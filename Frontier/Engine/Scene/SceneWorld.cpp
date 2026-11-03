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
#include <limits>
#include <unordered_map>

namespace Frontier
{

namespace
{

float ReadFloat(const toml::table& Table, std::string_view Key, float Fallback)
{
    const auto Quantity = Table[Key].value<double>();
    if (Quantity && std::isfinite(*Quantity) && std::abs(*Quantity) <= std::numeric_limits<float>::max())
        return static_cast<float>(*Quantity);
    return Fallback;
}

uint32_t ReadUint32(const toml::table& Table, std::string_view Key, uint32_t Fallback)
{
    if (const auto IntVal = Table[Key].value<int64_t>())
    {
        return static_cast<uint32_t>(std::clamp<int64_t>(*IntVal, 0, std::numeric_limits<uint32_t>::max()));
    }
    return Fallback;
}

int ReadInt(const toml::table& Table, std::string_view Key, int Fallback)
{
    if (const auto IntVal = Table[Key].value<int64_t>())
    {
        if (*IntVal >= std::numeric_limits<int>::min() && *IntVal <= std::numeric_limits<int>::max())
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
            if (const auto Val = (*Array)[Index].value<double>(); Val && std::isfinite(*Val) && std::abs(*Val) <= std::numeric_limits<float>::max())
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

void WriteAtmosphereOptics(toml::table& Table, const AtmosphereOpticalConfiguration& Optical)
{
    Table.insert_or_assign("rayleigh",double(Optical.RayleighScattering));
    Table.insert_or_assign("mie_turbidity",double(Optical.MieTurbidity));
    Table.insert_or_assign("mie_g",double(Optical.MieAnisotropyG));
    Table.insert_or_assign("ozone",double(Optical.OzoneAbsorptionScale));
    Table.insert_or_assign("planet_radius_km",double(Optical.PlanetRadiusKm));
    Table.insert_or_assign("atmosphere_thickness_km",double(Optical.AtmosphereThicknessKm));
    Table.insert_or_assign("rayleigh_height_m",double(Optical.RayleighHeightMetres));
    Table.insert_or_assign("mie_height_m",double(Optical.MieHeightMetres));
    WriteFloat3(Table,"ground_albedo",Optical.GroundAlbedo);
    Table.insert_or_assign("horizon_glow",double(Optical.HorizonGlow));
    Table.insert_or_assign("multiple_scattering",Optical.MultiScatteringLut);
    Table.insert_or_assign("tier_samples",int64_t(Optical.TierSamples));
}

void ReadAtmosphereOptics(const toml::table& Table, AtmosphereOpticalConfiguration& Optical)
{
    Optical.RayleighScattering = ReadFloat(Table,"rayleigh",Optical.RayleighScattering);
    Optical.MieTurbidity = ReadFloat(Table,"mie_turbidity",Optical.MieTurbidity);
    Optical.MieAnisotropyG = ReadFloat(Table,"mie_g",Optical.MieAnisotropyG);
    Optical.OzoneAbsorptionScale = ReadFloat(Table,"ozone",Optical.OzoneAbsorptionScale);
    Optical.PlanetRadiusKm = ReadFloat(Table,"planet_radius_km",Optical.PlanetRadiusKm);
    Optical.AtmosphereThicknessKm = ReadFloat(Table,"atmosphere_thickness_km",Optical.AtmosphereThicknessKm);
    Optical.RayleighHeightMetres = ReadFloat(Table,"rayleigh_height_m",Optical.RayleighHeightMetres);
    Optical.MieHeightMetres = ReadFloat(Table,"mie_height_m",Optical.MieHeightMetres);
    ReadFloat3(Table,"ground_albedo",Optical.GroundAlbedo);
    Optical.HorizonGlow = ReadFloat(Table,"horizon_glow",Optical.HorizonGlow);
    Optical.MultiScatteringLut = ReadBool(Table,"multiple_scattering",Optical.MultiScatteringLut);
    Optical.TierSamples = ReadUint32(Table,"tier_samples",Optical.TierSamples);
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
                                                  const char* SubtitleCategory,
                                                  const char* AccentHex)
{
    (void)GlyphIdentifier;
    (void)GroupName;
    (void)SubtitleCategory;
    (void)AccentHex;
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
    if (AccentHex != nullptr && AccentHex[0] != '\0')
    {
        Meta.AccentHex = AccentHex;
    }
    SceneDirectory::Instance().ConfigureMetadata(Token(), std::move(Meta));
#endif
}

//------------------------------------------------------------------------------------------------------------------------
//                                       SINGLE-CONFIGURATION SERIALIZATION SPECIALIZATIONS
//------------------------------------------------------------------------------------------------------------------------

template <>
void RenderPipelineRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("hardware_ray_query", Parameters.HardwareRayQueryTier);
    OutTable.insert_or_assign("analytic_illumination", Parameters.AnalyticIllumination);
    OutTable.insert_or_assign("global_illumination", Parameters.GlobalIllumination);
    OutTable.insert_or_assign("multi_bounce_nee", Parameters.MultiBounceNee);
    OutTable.insert_or_assign("russian_roulette", Parameters.RussianRoulette);
    OutTable.insert_or_assign("temporal_reuse", Parameters.TemporalReuse);
    OutTable.insert_or_assign("spatial_reuse", Parameters.SpatialReuse);
    OutTable.insert_or_assign("visibility_reuse", Parameters.VisibilityReuse);
    OutTable.insert_or_assign("radiance_refresh", Parameters.RadianceRefresh);
    OutTable.insert_or_assign("card_radiosity", Parameters.CardRadiosity);
    OutTable.insert_or_assign("reflections", Parameters.Reflection);
    OutTable.insert_or_assign("ray_traced_reflections", Parameters.RayTracedReflections);
    OutTable.insert_or_assign("mirror_filter", Parameters.MirrorFilter);
    OutTable.insert_or_assign("mirror_trace", Parameters.MirrorTrace);
    OutTable.insert_or_assign("direct_shadow", Parameters.DirectShadow);
    OutTable.insert_or_assign("ray_traced_shadows", Parameters.RayTracedShadows);
    OutTable.insert_or_assign("firefly_filter", Parameters.FireflyFilter);
    OutTable.insert_or_assign("solar_disc", Parameters.SolarDisc);
    OutTable.insert_or_assign("stratified_sun", Parameters.StratifiedSun);
    OutTable.insert_or_assign("penumbra_filter", Parameters.PenumbraFilter);
    OutTable.insert_or_assign("variance_filter", Parameters.VarianceFilter);
    OutTable.insert_or_assign("temporal_aa", Parameters.TemporalAntiAliasing);
    OutTable.insert_or_assign("auto_exposure", Parameters.AutoExposure);
    OutTable.insert_or_assign("virtualized_geometry", Parameters.VirtualizedGeometry);
    OutTable.insert_or_assign("live_refresh", Parameters.LiveRefresh);
    OutTable.insert_or_assign("quality_tier", static_cast<int64_t>(Parameters.QualityTier));
    OutTable.insert_or_assign("trace_scale", static_cast<int64_t>(Parameters.TraceScale));
    OutTable.insert_or_assign("initial_candidates", static_cast<int64_t>(Parameters.InitialCandidates));
    OutTable.insert_or_assign("max_bounces", static_cast<int64_t>(Parameters.MaxBounces));
    OutTable.insert_or_assign("spatial_count", static_cast<int64_t>(Parameters.SpatialCount));
    OutTable.insert_or_assign("card_tile_budget", static_cast<int64_t>(Parameters.CardTileBudget));
    OutTable.insert_or_assign("reflection_bounces", static_cast<int64_t>(Parameters.ReflectionBounces));
    OutTable.insert_or_assign("diffuse_wavelet_levels", static_cast<int64_t>(Parameters.DiffuseWaveletLevels));
    OutTable.insert_or_assign("diffuse_tally_limit", static_cast<int64_t>(Parameters.DiffuseTallyLimit));
    OutTable.insert_or_assign("glossy_wavelet_levels", static_cast<int64_t>(Parameters.GlossyWaveletLevels));
    OutTable.insert_or_assign("glossy_tally_limit", static_cast<int64_t>(Parameters.GlossyTallyLimit));
    OutTable.insert_or_assign("virtualized_stream_slots", static_cast<int64_t>(Parameters.VirtualizedStreamSlots));
    OutTable.insert_or_assign("inspection_mode", static_cast<int64_t>(Parameters.InspectionModeIndex));
    OutTable.insert_or_assign("distance_inspection", static_cast<int64_t>(Parameters.DistanceInspection));
    OutTable.insert_or_assign("target_rate", static_cast<int64_t>(Parameters.TargetRate));
    OutTable.insert_or_assign("render_scale", static_cast<double>(Parameters.RenderScale));
    OutTable.insert_or_assign("spatial_radius", static_cast<double>(Parameters.SpatialRadius));
    OutTable.insert_or_assign("temporal_tally_limit", static_cast<double>(Parameters.TemporalTallyLimit));
    OutTable.insert_or_assign("indirect_scale", static_cast<double>(Parameters.IndirectScale));
    OutTable.insert_or_assign("sky_scale", static_cast<double>(Parameters.SkyScale));
    OutTable.insert_or_assign("incandescence_scale", static_cast<double>(Parameters.IncandescenceScale));
    OutTable.insert_or_assign("reflection_scale", static_cast<double>(Parameters.ReflectionScale));
    OutTable.insert_or_assign("reflection_roughness", static_cast<double>(Parameters.ReflectionRoughness));
    OutTable.insert_or_assign("diffuse_minimum_weight", static_cast<double>(Parameters.DiffuseMinimumWeight));
    OutTable.insert_or_assign("diffuse_colour_sigma", static_cast<double>(Parameters.DiffuseColourSigma));
    OutTable.insert_or_assign("glossy_minimum_weight", static_cast<double>(Parameters.GlossyMinimumWeight));
    OutTable.insert_or_assign("glossy_colour_sigma", static_cast<double>(Parameters.GlossyColourSigma));
    OutTable.insert_or_assign("exposure_ev", static_cast<double>(Parameters.ExposureCompensationEv));
}

// 📝 The bloom, filmic tonemap, screen-space reflection and probe-range keys of earlier levels had no reader; they are ignored.
template <>
void RenderPipelineRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled                = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.HardwareRayQueryTier   = ReadBool(InTable, "hardware_ray_query", Parameters.HardwareRayQueryTier);
    Parameters.AnalyticIllumination   = ReadBool(InTable, "analytic_illumination", Parameters.AnalyticIllumination);
    Parameters.GlobalIllumination     = ReadBool(InTable, "global_illumination", Parameters.GlobalIllumination);
    Parameters.MultiBounceNee         = ReadBool(InTable, "multi_bounce_nee", Parameters.MultiBounceNee);
    Parameters.RussianRoulette        = ReadBool(InTable, "russian_roulette", Parameters.RussianRoulette);
    Parameters.TemporalReuse          = ReadBool(InTable, "temporal_reuse", Parameters.TemporalReuse);
    Parameters.SpatialReuse           = ReadBool(InTable, "spatial_reuse", Parameters.SpatialReuse);
    Parameters.VisibilityReuse        = ReadBool(InTable, "visibility_reuse", Parameters.VisibilityReuse);
    Parameters.RadianceRefresh        = ReadBool(InTable, "radiance_refresh", Parameters.RadianceRefresh);
    Parameters.CardRadiosity          = ReadBool(InTable, "card_radiosity", Parameters.CardRadiosity);
    Parameters.Reflection             = ReadBool(InTable, "reflections", Parameters.Reflection);
    Parameters.RayTracedReflections   = ReadBool(InTable, "ray_traced_reflections", Parameters.RayTracedReflections);
    Parameters.MirrorFilter           = ReadBool(InTable, "mirror_filter", Parameters.MirrorFilter);
    Parameters.MirrorTrace            = ReadBool(InTable, "mirror_trace", Parameters.MirrorTrace);
    Parameters.DirectShadow           = ReadBool(InTable, "direct_shadow", Parameters.DirectShadow);
    Parameters.RayTracedShadows       = ReadBool(InTable, "ray_traced_shadows", Parameters.RayTracedShadows);
    Parameters.FireflyFilter          = ReadBool(InTable, "firefly_filter", Parameters.FireflyFilter);
    Parameters.SolarDisc              = ReadBool(InTable, "solar_disc", Parameters.SolarDisc);
    Parameters.StratifiedSun          = ReadBool(InTable, "stratified_sun", Parameters.StratifiedSun);
    Parameters.PenumbraFilter         = ReadBool(InTable, "penumbra_filter", Parameters.PenumbraFilter);
    Parameters.VarianceFilter         = ReadBool(InTable, "variance_filter", Parameters.VarianceFilter);
    Parameters.TemporalAntiAliasing   = ReadBool(InTable, "temporal_aa", Parameters.TemporalAntiAliasing);
    Parameters.AutoExposure           = ReadBool(InTable, "auto_exposure", Parameters.AutoExposure);
    Parameters.VirtualizedGeometry    = ReadBool(InTable, "virtualized_geometry", Parameters.VirtualizedGeometry);
    Parameters.LiveRefresh            = ReadBool(InTable, "live_refresh", Parameters.LiveRefresh);
    Parameters.QualityTier            = std::clamp(ReadInt(InTable, "quality_tier", Parameters.QualityTier), 0, 2);
    Parameters.TraceScale             = std::clamp(ReadInt(InTable, "trace_scale", Parameters.TraceScale), 1, 4);
    Parameters.InitialCandidates      = std::clamp(ReadInt(InTable, "initial_candidates", Parameters.InitialCandidates), 1, 4);
    Parameters.MaxBounces             = std::clamp(ReadInt(InTable, "max_bounces", Parameters.MaxBounces), 1, 4);
    Parameters.SpatialCount           = std::clamp(ReadInt(InTable, "spatial_count", Parameters.SpatialCount), 0, 8);
    Parameters.CardTileBudget         = std::clamp(ReadInt(InTable, "card_tile_budget", Parameters.CardTileBudget), 0, 8192);
    Parameters.ReflectionBounces      = std::clamp(ReadInt(InTable, "reflection_bounces", Parameters.ReflectionBounces), 1, 4);
    Parameters.DiffuseWaveletLevels   = std::clamp(ReadInt(InTable, "diffuse_wavelet_levels", Parameters.DiffuseWaveletLevels), 1, 4);
    Parameters.DiffuseTallyLimit      = std::clamp(ReadInt(InTable, "diffuse_tally_limit", Parameters.DiffuseTallyLimit), 1, 64);
    Parameters.GlossyWaveletLevels    = std::clamp(ReadInt(InTable, "glossy_wavelet_levels", Parameters.GlossyWaveletLevels), 1, 4);
    Parameters.GlossyTallyLimit       = std::clamp(ReadInt(InTable, "glossy_tally_limit", Parameters.GlossyTallyLimit), 1, 64);
    Parameters.VirtualizedStreamSlots = std::clamp(ReadInt(InTable, "virtualized_stream_slots", Parameters.VirtualizedStreamSlots), 0, 64);
    Parameters.InspectionModeIndex    = std::clamp(ReadInt(InTable, "inspection_mode", Parameters.InspectionModeIndex), 0, 14);
    Parameters.DistanceInspection     = std::clamp(ReadInt(InTable, "distance_inspection", Parameters.DistanceInspection), 1, 3);
    Parameters.TargetRate             = std::clamp(ReadInt(InTable, "target_rate", Parameters.TargetRate), 30, 120);
    Parameters.RenderScale            = std::clamp(ReadFloat(InTable, "render_scale", Parameters.RenderScale), 0.25f, 2.0f);
    Parameters.SpatialRadius          = std::clamp(ReadFloat(InTable, "spatial_radius", Parameters.SpatialRadius), 2.0f, 48.0f);
    Parameters.TemporalTallyLimit     = std::clamp(ReadFloat(InTable, "temporal_tally_limit", Parameters.TemporalTallyLimit), 1.0f, 40.0f);
    Parameters.IndirectScale          = std::clamp(ReadFloat(InTable, "indirect_scale", Parameters.IndirectScale), 0.0f, 3.0f);
    Parameters.SkyScale               = std::clamp(ReadFloat(InTable, "sky_scale", Parameters.SkyScale), 0.0f, 3.0f);
    Parameters.IncandescenceScale     = std::clamp(ReadFloat(InTable, "incandescence_scale", Parameters.IncandescenceScale), 0.0f, 3.0f);
    Parameters.ReflectionScale        = std::clamp(ReadFloat(InTable, "reflection_scale", Parameters.ReflectionScale), 0.0f, 3.0f);
    Parameters.ReflectionRoughness    = std::clamp(ReadFloat(InTable, "reflection_roughness", Parameters.ReflectionRoughness), 0.15f, 1.0f);
    Parameters.DiffuseMinimumWeight   = std::clamp(ReadFloat(InTable, "diffuse_minimum_weight", Parameters.DiffuseMinimumWeight), 0.01f, 0.5f);
    Parameters.DiffuseColourSigma     = std::clamp(ReadFloat(InTable, "diffuse_colour_sigma", Parameters.DiffuseColourSigma), 0.5f, 16.0f);
    Parameters.GlossyMinimumWeight    = std::clamp(ReadFloat(InTable, "glossy_minimum_weight", Parameters.GlossyMinimumWeight), 0.01f, 0.5f);
    Parameters.GlossyColourSigma      = std::clamp(ReadFloat(InTable, "glossy_colour_sigma", Parameters.GlossyColourSigma), 0.5f, 16.0f);
    Parameters.ExposureCompensationEv = std::clamp(ReadFloat(InTable, "exposure_ev", Parameters.ExposureCompensationEv), -4.0f, 4.0f);
    // 📝 Only the demo's three trace resolutions: full, half and quarter.
    if (Parameters.TraceScale == 3) Parameters.TraceScale = 2;
    Parameters.AdvanceRevision();
}

template <>
void SolarRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("elevation_deg", static_cast<double>(Parameters.ElevationDegrees));
    OutTable.insert_or_assign("azimuth_deg", static_cast<double>(Parameters.AzimuthDegrees));
    OutTable.insert_or_assign("illuminance_klx", static_cast<double>(Parameters.IlluminanceKilolux));
    OutTable.insert_or_assign("angular_diameter_deg", static_cast<double>(Parameters.AngularDiameterDeg));
    OutTable.insert_or_assign("kelvin", static_cast<double>(Parameters.KelvinTemperature));
    OutTable.insert_or_assign("ray_shadow", Parameters.CastRaytracedShadow);
    OutTable.insert_or_assign("light_shafts", Parameters.VolumetricLightShaft);
    OutTable.insert_or_assign("disc_visible", Parameters.DiscVisible);
    OutTable.insert_or_assign("aureole_gain", static_cast<double>(Parameters.AureoleGain));
    OutTable.insert_or_assign("colour_source", static_cast<int64_t>(Parameters.ColourSource));
    WriteFloat3(OutTable, "tint_linear", Parameters.TintLinear.data());
    OutTable.insert_or_assign("time_linked", Parameters.TimeLinked);
    OutTable.insert_or_assign("day_cycle", Parameters.DayCycle);
    OutTable.insert_or_assign("local_solar_h", static_cast<double>(Parameters.LocalSolarHours));
    OutTable.insert_or_assign("day_duration_h", static_cast<double>(Parameters.DayDurationHours));
    OutTable.insert_or_assign("calendar_date", Parameters.CalendarDate);
    OutTable.insert_or_assign("latitude_deg", static_cast<double>(Parameters.LatitudeDegrees));
    OutTable.insert_or_assign("longitude_deg", static_cast<double>(Parameters.LongitudeDegrees));
    OutTable.insert_or_assign("utc_offset_h", static_cast<double>(Parameters.UtcOffsetHours));
}

template <>
void SolarRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.ElevationDegrees     = ReadFloat(InTable, "elevation_deg", Parameters.ElevationDegrees);
    Parameters.AzimuthDegrees       = ReadFloat(InTable, "azimuth_deg", Parameters.AzimuthDegrees);
    Parameters.IlluminanceKilolux   = ReadFloat(InTable, "illuminance_klx", Parameters.IlluminanceKilolux);
    Parameters.AngularDiameterDeg   = ReadFloat(InTable, "angular_diameter_deg", Parameters.AngularDiameterDeg);
    Parameters.KelvinTemperature    = ReadFloat(InTable, "kelvin", Parameters.KelvinTemperature);
    Parameters.CastRaytracedShadow  = ReadBool(InTable, "ray_shadow", Parameters.CastRaytracedShadow);
    Parameters.VolumetricLightShaft = ReadBool(InTable, "light_shafts", Parameters.VolumetricLightShaft);
    Parameters.DiscVisible          = ReadBool(InTable, "disc_visible", Parameters.DiscVisible);
    Parameters.AureoleGain          = ReadFloat(InTable, "aureole_gain", Parameters.AureoleGain);
    Parameters.ColourSource         = std::min<uint32_t>(1u, ReadUint32(InTable, "colour_source", Parameters.ColourSource));
    if (InTable.contains("tint_linear")) ReadFloat3(InTable, "tint_linear", Parameters.TintLinear.data());
    Parameters.TimeLinked           = ReadBool(InTable, "time_linked", Parameters.TimeLinked);
    Parameters.DayCycle             = ReadBool(InTable, "day_cycle", Parameters.DayCycle);
    Parameters.LocalSolarHours      = ReadFloat(InTable, "local_solar_h", Parameters.LocalSolarHours);
    Parameters.DayDurationHours     = ReadFloat(InTable, "day_duration_h", Parameters.DayDurationHours);
    if (const auto Date = InTable["calendar_date"].value<std::string>()) Parameters.CalendarDate = *Date;
    Parameters.LatitudeDegrees      = ReadFloat(InTable, "latitude_deg", Parameters.LatitudeDegrees);
    Parameters.LongitudeDegrees     = ReadFloat(InTable, "longitude_deg", Parameters.LongitudeDegrees);
    Parameters.UtcOffsetHours       = ReadFloat(InTable, "utc_offset_h", Parameters.UtcOffsetHours);
    Parameters.AdvanceRevision();
}

template <>
void OpticalFlareRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("intensity", static_cast<double>(Parameters.Intensity));
    OutTable.insert_or_assign("aperture_blades", static_cast<int64_t>(Parameters.ApertureBladeCount));
    OutTable.insert_or_assign("luminance_threshold", static_cast<double>(Parameters.LuminanceThreshold));
    OutTable.insert_or_assign("halo_radius", static_cast<double>(Parameters.HaloRadius));
    OutTable.insert_or_assign("chromatic_dispersion", static_cast<double>(Parameters.ChromaticDispersion));
    OutTable.insert_or_assign("anamorphic_streak", Parameters.AnamorphicStreak);
    OutTable.insert_or_assign("diffraction_starburst", Parameters.DiffractionStarburst);
    OutTable.insert_or_assign("preset", static_cast<int64_t>(Parameters.Preset));
    OutTable.insert_or_assign("spread", static_cast<double>(Parameters.Spread));
    OutTable.insert_or_assign("ghost_count", static_cast<int64_t>(Parameters.GhostCount));
    OutTable.insert_or_assign("ghost_brightness", static_cast<double>(Parameters.GhostBrightness));
    OutTable.insert_or_assign("ghost_shape", static_cast<int64_t>(Parameters.GhostShape));
    OutTable.insert_or_assign("ghost_spacing", static_cast<double>(Parameters.GhostSpacing));
    OutTable.insert_or_assign("halo_width", static_cast<double>(Parameters.HaloWidth));
    OutTable.insert_or_assign("halo_brightness", static_cast<double>(Parameters.HaloBrightness));
    OutTable.insert_or_assign("streak_strength", static_cast<double>(Parameters.StreakStrength));
    OutTable.insert_or_assign("rotation_degrees", static_cast<double>(Parameters.RotationDegrees));
    OutTable.insert_or_assign("ray_count", static_cast<int64_t>(Parameters.RayCount));
    OutTable.insert_or_assign("warm_streak", Parameters.WarmStreak);
    OutTable.insert_or_assign("preview_position_x", static_cast<double>(Parameters.PreviewPosition[0]));
    OutTable.insert_or_assign("preview_position_y", static_cast<double>(Parameters.PreviewPosition[1]));
}

template <>
void OpticalFlareRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled            = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.Intensity          = ReadFloat(InTable, "intensity", Parameters.Intensity);
    Parameters.ApertureBladeCount = ReadUint32(InTable, "aperture_blades", Parameters.ApertureBladeCount);
    Parameters.LuminanceThreshold = ReadFloat(InTable, "luminance_threshold", Parameters.LuminanceThreshold);
    Parameters.HaloRadius = ReadFloat(InTable, "halo_radius", Parameters.HaloRadius);
    Parameters.ChromaticDispersion = ReadFloat(InTable, "chromatic_dispersion", Parameters.ChromaticDispersion);
    Parameters.AnamorphicStreak = ReadBool(InTable, "anamorphic_streak", Parameters.AnamorphicStreak);
    Parameters.DiffractionStarburst = ReadBool(InTable, "diffraction_starburst", Parameters.DiffractionStarburst);
    Parameters.Preset = ReadUint32(InTable, "preset", Parameters.Preset);
    Parameters.Spread = ReadFloat(InTable, "spread", Parameters.Spread);
    Parameters.GhostCount = ReadUint32(InTable, "ghost_count", Parameters.GhostCount);
    Parameters.GhostBrightness = ReadFloat(InTable, "ghost_brightness", Parameters.GhostBrightness);
    Parameters.GhostShape = ReadUint32(InTable, "ghost_shape", Parameters.GhostShape);
    Parameters.GhostSpacing = ReadFloat(InTable, "ghost_spacing", Parameters.GhostSpacing);
    Parameters.HaloWidth = ReadFloat(InTable, "halo_width", Parameters.HaloWidth);
    Parameters.HaloBrightness = ReadFloat(InTable, "halo_brightness", Parameters.HaloBrightness);
    Parameters.StreakStrength = ReadFloat(InTable, "streak_strength", Parameters.StreakStrength);
    Parameters.RotationDegrees = ReadFloat(InTable, "rotation_degrees", Parameters.RotationDegrees);
    Parameters.RayCount = ReadUint32(InTable, "ray_count", Parameters.RayCount);
    Parameters.WarmStreak = ReadBool(InTable, "warm_streak", Parameters.WarmStreak);
    Parameters.PreviewPosition[0] = ReadFloat(InTable, "preview_position_x", Parameters.PreviewPosition[0]);
    Parameters.PreviewPosition[1] = ReadFloat(InTable, "preview_position_y", Parameters.PreviewPosition[1]);
    Parameters.AdvanceRevision();
}

template <>
void AtmosphereRecord::SerializeConfiguration(toml::table& OutTable) const
{
    WriteAtmosphereOptics(OutTable,Parameters);
    OutTable.insert_or_assign("aerial_perspective",Parameters.AerialPerspective);
    OutTable.insert_or_assign("use_baked",Parameters.UseBaked);
    if (Parameters.BakedAvailable)
    {
        toml::table Baked;
        WriteAtmosphereOptics(Baked,Parameters.Baked);
        OutTable.insert_or_assign("baked",std::move(Baked));
    }

    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("rayleigh", static_cast<double>(Parameters.RayleighScattering));
    OutTable.insert_or_assign("mie_turbidity", static_cast<double>(Parameters.MieTurbidity));
    OutTable.insert_or_assign("ozone", static_cast<double>(Parameters.OzoneAbsorptionScale));
    OutTable.insert_or_assign("mie_g", static_cast<double>(Parameters.MieAnisotropyG));
    OutTable.insert_or_assign("rayleigh_height_m", static_cast<double>(Parameters.RayleighHeightMetres));
    OutTable.insert_or_assign("mie_height_m", static_cast<double>(Parameters.MieHeightMetres));
    WriteFloat3(OutTable, "ground_albedo", Parameters.GroundAlbedo);
    WriteFloat3(OutTable, "sky_tint", Parameters.SkyTint);
    OutTable.insert_or_assign("sky_scale", static_cast<double>(Parameters.SkyScale));
    OutTable.insert_or_assign("horizon_glow", static_cast<double>(Parameters.HorizonGlow));
    OutTable.insert_or_assign("white_line", static_cast<double>(Parameters.WhiteLine));
    OutTable.insert_or_assign("line_at_civil_only", Parameters.LineAtCivilOnly);
    OutTable.insert_or_assign("air_mass", Parameters.AirMass);
    OutTable.insert_or_assign("multiple_scattering", Parameters.MultiScatteringLut);
    OutTable.insert_or_assign("tier_samples", static_cast<int64_t>(Parameters.TierSamples));
}

template <>
void AtmosphereRecord::DeserializeConfiguration(const toml::table& InTable)
{
    ReadAtmosphereOptics(InTable,Parameters);
    Parameters.AerialPerspective = ReadBool(InTable,"aerial_perspective",Parameters.AerialPerspective);
    Parameters.UseBaked = ReadBool(InTable,"use_baked",Parameters.UseBaked);
    if (const auto* Baked = InTable["baked"].as_table())
    {
        ReadAtmosphereOptics(*Baked,Parameters.Baked);
        Parameters.BakedAvailable = true;
    }

    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.RayleighScattering   = ReadFloat(InTable, "rayleigh", Parameters.RayleighScattering);
    Parameters.MieTurbidity         = ReadFloat(InTable, "mie_turbidity", Parameters.MieTurbidity);
    Parameters.OzoneAbsorptionScale = ReadFloat(InTable, "ozone", Parameters.OzoneAbsorptionScale);
    Parameters.MieAnisotropyG       = ReadFloat(InTable, "mie_g", Parameters.MieAnisotropyG);
    Parameters.RayleighHeightMetres = ReadFloat(InTable, "rayleigh_height_m", Parameters.RayleighHeightMetres);
    Parameters.MieHeightMetres      = ReadFloat(InTable, "mie_height_m", Parameters.MieHeightMetres);
    ReadFloat3(InTable, "ground_albedo", Parameters.GroundAlbedo);
    ReadFloat3(InTable, "sky_tint", Parameters.SkyTint);
    Parameters.SkyScale             = ReadFloat(InTable, "sky_scale", Parameters.SkyScale);
    Parameters.HorizonGlow          = ReadFloat(InTable, "horizon_glow", Parameters.HorizonGlow);
    Parameters.WhiteLine            = ReadFloat(InTable, "white_line", Parameters.WhiteLine);
    Parameters.LineAtCivilOnly      = ReadBool(InTable, "line_at_civil_only", Parameters.LineAtCivilOnly);
    Parameters.AirMass              = ReadBool(InTable, "air_mass", Parameters.AirMass);
    Parameters.MultiScatteringLut   = ReadBool(InTable, "multiple_scattering", Parameters.MultiScatteringLut);
    Parameters.TierSamples          = ReadUint32(InTable, "tier_samples", Parameters.TierSamples);
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
    OutTable.insert_or_assign("density", static_cast<double>(Parameters.ExtinctionDensity));
    OutTable.insert_or_assign("drift_mps", static_cast<double>(Parameters.WindDriftSpeed));
    OutTable.insert_or_assign("silver_lining", static_cast<double>(Parameters.SilverLiningPower));
    OutTable.insert_or_assign("temporal_reprojection", Parameters.TemporalReprojection);
    OutTable.insert_or_assign("cast_shadows", Parameters.CastGroundShadows);
    OutTable.insert_or_assign("formation", static_cast<int64_t>(Parameters.Formation));
    OutTable.insert_or_assign("ceiling_metres", static_cast<double>(Parameters.CeilingMetres));
    OutTable.insert_or_assign("feature_scale", static_cast<double>(Parameters.FeatureScale));
    OutTable.insert_or_assign("anvil_shaping", static_cast<double>(Parameters.AnvilShaping));
    OutTable.insert_or_assign("edge_erosion", static_cast<double>(Parameters.EdgeErosion));
    OutTable.insert_or_assign("phase_anisotropy", static_cast<double>(Parameters.PhaseAnisotropy));
    OutTable.insert_or_assign("light_absorption", static_cast<double>(Parameters.LightAbsorption));
    OutTable.insert_or_assign("powder_response", static_cast<double>(Parameters.PowderResponse));
    OutTable.insert_or_assign("light_samples", static_cast<int64_t>(Parameters.LightSamples));
    OutTable.insert_or_assign("resolution_percent", static_cast<int64_t>(Parameters.ResolutionPercent));
    OutTable.insert_or_assign("sky_samples", static_cast<int64_t>(Parameters.SkySamples));
    OutTable.insert_or_assign("local_samples", static_cast<int64_t>(Parameters.LocalSamples));
    OutTable.insert_or_assign("shadow_strength", static_cast<double>(Parameters.ShadowStrength));
    OutTable.insert_or_assign("shadow_softness_metres", static_cast<double>(Parameters.ShadowSoftnessMetres));
    OutTable.insert_or_assign("shadow_span_metres", static_cast<double>(Parameters.ShadowSpanMetres));
    OutTable.insert_or_assign("shadow_samples", static_cast<int64_t>(Parameters.ShadowSamples));
    OutTable.insert_or_assign("follow_wind", Parameters.FollowWind);
    OutTable.insert_or_assign("bounds_shape", static_cast<int64_t>(Parameters.BoundsShape));
    OutTable.insert_or_assign("local_formation", static_cast<int64_t>(Parameters.LocalFormation));
    OutTable.insert_or_assign("edge_softness", static_cast<double>(Parameters.EdgeSoftness));
    OutTable.insert_or_assign("local_feature_metres", static_cast<double>(Parameters.LocalFeatureMetres));
    WriteFloat3(OutTable, "centre_metres", Parameters.CentreMetres);
    WriteFloat3(OutTable, "half_extents_metres", Parameters.HalfExtentsMetres);
}

template <>
void CloudRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.LocalVolume          = ReadBool(InTable, "local_volume", Parameters.LocalVolume);
    Parameters.Coverage             = ReadFloat(InTable, "coverage", Parameters.Coverage);
    Parameters.BaseAltitudeMetres   = ReadFloat(InTable, "base_altitude_m", Parameters.BaseAltitudeMetres);
    Parameters.LayerThicknessMetres = ReadFloat(InTable, "thickness_m", Parameters.LayerThicknessMetres);
    Parameters.ExtinctionDensity = ReadFloat(InTable, "density", Parameters.ExtinctionDensity);
    Parameters.WindDriftSpeed = ReadFloat(InTable, "drift_mps", Parameters.WindDriftSpeed);
    Parameters.SilverLiningPower = ReadFloat(InTable, "silver_lining", Parameters.SilverLiningPower);
    Parameters.TemporalReprojection = ReadBool(InTable, "temporal_reprojection", Parameters.TemporalReprojection);
    Parameters.CastGroundShadows = ReadBool(InTable, "cast_shadows", Parameters.CastGroundShadows);
    Parameters.Formation = ReadUint32(InTable, "formation", Parameters.Formation);
    Parameters.CeilingMetres = ReadFloat(InTable, "ceiling_metres", Parameters.CeilingMetres);
    Parameters.FeatureScale = ReadFloat(InTable, "feature_scale", Parameters.FeatureScale);
    Parameters.AnvilShaping = ReadFloat(InTable, "anvil_shaping", Parameters.AnvilShaping);
    Parameters.EdgeErosion = ReadFloat(InTable, "edge_erosion", Parameters.EdgeErosion);
    Parameters.PhaseAnisotropy = ReadFloat(InTable, "phase_anisotropy", Parameters.PhaseAnisotropy);
    Parameters.LightAbsorption = ReadFloat(InTable, "light_absorption", Parameters.LightAbsorption);
    Parameters.PowderResponse = ReadFloat(InTable, "powder_response", Parameters.PowderResponse);
    Parameters.LightSamples = ReadUint32(InTable, "light_samples", Parameters.LightSamples);
    Parameters.ResolutionPercent = ReadUint32(InTable, "resolution_percent", Parameters.ResolutionPercent);
    Parameters.SkySamples = ReadUint32(InTable, "sky_samples", Parameters.SkySamples);
    Parameters.LocalSamples = ReadUint32(InTable, "local_samples", Parameters.LocalSamples);
    Parameters.ShadowStrength = ReadFloat(InTable, "shadow_strength", Parameters.ShadowStrength);
    Parameters.ShadowSoftnessMetres = ReadFloat(InTable, "shadow_softness_metres", Parameters.ShadowSoftnessMetres);
    Parameters.ShadowSpanMetres = ReadFloat(InTable, "shadow_span_metres", Parameters.ShadowSpanMetres);
    Parameters.ShadowSamples = ReadUint32(InTable, "shadow_samples", Parameters.ShadowSamples);
    Parameters.FollowWind = ReadBool(InTable, "follow_wind", Parameters.FollowWind);
    Parameters.BoundsShape = ReadUint32(InTable, "bounds_shape", Parameters.BoundsShape);
    Parameters.LocalFormation = ReadUint32(InTable, "local_formation", Parameters.LocalFormation);
    Parameters.EdgeSoftness = ReadFloat(InTable, "edge_softness", Parameters.EdgeSoftness);
    Parameters.LocalFeatureMetres = ReadFloat(InTable, "local_feature_metres", Parameters.LocalFeatureMetres);
    ReadFloat3(InTable, "centre_metres", Parameters.CentreMetres);
    ReadFloat3(InTable, "half_extents_metres", Parameters.HalfExtentsMetres);
    Parameters.AdvanceRevision();
}

template <>
void StarRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("hipparcos_count", static_cast<int64_t>(Parameters.HipparcosCount));
    OutTable.insert_or_assign("magnitude_gain", static_cast<double>(Parameters.MagnitudeGain));
    OutTable.insert_or_assign("milky_way", static_cast<double>(Parameters.MilkyWayLuminance));
    OutTable.insert_or_assign("sidereal_speed", static_cast<double>(Parameters.SiderealSpeed));
    OutTable.insert_or_assign("twinkle_depth", static_cast<double>(Parameters.AtmosphericScintillate));
    OutTable.insert_or_assign("limiting_magnitude", static_cast<double>(Parameters.LimitingMagnitude));
    OutTable.insert_or_assign("point_size", static_cast<double>(Parameters.PointSize));
    OutTable.insert_or_assign("halo_gain", static_cast<double>(Parameters.HaloGain));
    OutTable.insert_or_assign("rotation_degrees", static_cast<double>(Parameters.RotationDegrees));
    OutTable.insert_or_assign("scintillation_enabled", Parameters.ScintillationEnabled);
    OutTable.insert_or_assign("scintillation_rate_hz", static_cast<double>(Parameters.ScintillationRateHz));
}

template <>
void StarRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled        = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.HipparcosCount = ReadUint32(InTable, "hipparcos_count", Parameters.HipparcosCount);
    Parameters.MagnitudeGain  = ReadFloat(InTable, "magnitude_gain", Parameters.MagnitudeGain);
    Parameters.MilkyWayLuminance = ReadFloat(InTable, "milky_way", Parameters.MilkyWayLuminance);
    Parameters.SiderealSpeed = ReadFloat(InTable, "sidereal_speed", Parameters.SiderealSpeed);
    Parameters.AtmosphericScintillate = ReadFloat(InTable, "twinkle_depth", Parameters.AtmosphericScintillate);
    Parameters.LimitingMagnitude = ReadFloat(InTable, "limiting_magnitude", Parameters.LimitingMagnitude);
    Parameters.PointSize = ReadFloat(InTable, "point_size", Parameters.PointSize);
    Parameters.HaloGain = ReadFloat(InTable, "halo_gain", Parameters.HaloGain);
    Parameters.RotationDegrees = ReadFloat(InTable, "rotation_degrees", Parameters.RotationDegrees);
    Parameters.ScintillationEnabled = ReadBool(InTable, "scintillation_enabled", Parameters.ScintillationEnabled);
    Parameters.ScintillationRateHz = ReadFloat(InTable, "scintillation_rate_hz", Parameters.ScintillationRateHz);
    Parameters.AdvanceRevision();
}

template <>
void MoonRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("primary_phase", static_cast<double>(Parameters.PrimaryPhase));
    OutTable.insert_or_assign("primary_orbit_degrees", static_cast<double>(Parameters.PrimaryOrbitDegrees));
    OutTable.insert_or_assign("primary_albedo", static_cast<double>(Parameters.PrimaryAlbedo));
    OutTable.insert_or_assign("secondary_enabled", Parameters.SecondaryMoonEnabled);
    OutTable.insert_or_assign("secondary_orbit_degrees", static_cast<double>(Parameters.SecondaryOrbitDegrees));
    OutTable.insert_or_assign("earthshine", static_cast<double>(Parameters.EarthshineLuminance));
    OutTable.insert_or_assign("illuminate_scene", Parameters.IlluminateScene);
    OutTable.insert_or_assign("light_power", static_cast<double>(Parameters.LightPower));
    toml::array Instances;
    for (const auto& Moon : Parameters.Instances)
    {
        toml::table Entry;
        Entry.insert_or_assign("visible", Moon.Visible);
        Entry.insert_or_assign("follow_sky", Moon.FollowSky);
        Entry.insert_or_assign("body", static_cast<int64_t>(Moon.Body));
        Entry.insert_or_assign("phase_days", static_cast<double>(Moon.PhaseDays));
        Entry.insert_or_assign("azimuth_degrees", static_cast<double>(Moon.AzimuthDegrees));
        Entry.insert_or_assign("elevation_degrees", static_cast<double>(Moon.ElevationDegrees));
        Entry.insert_or_assign("roll_degrees", static_cast<double>(Moon.RollDegrees));
        Entry.insert_or_assign("pitch_degrees", static_cast<double>(Moon.PitchDegrees));
        Entry.insert_or_assign("spin_degrees", static_cast<double>(Moon.SpinDegrees));
        Entry.insert_or_assign("angular_diameter_degrees", static_cast<double>(Moon.AngularDiameterDegrees));
        Entry.insert_or_assign("brightness", static_cast<double>(Moon.Brightness));
        Entry.insert_or_assign("glow", static_cast<double>(Moon.Glow));
        Instances.push_back(std::move(Entry));
    }
    OutTable.insert_or_assign("instances", std::move(Instances));
}

template <>
void MoonRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.PrimaryPhase = ReadFloat(InTable, "primary_phase", Parameters.PrimaryPhase);
    Parameters.PrimaryOrbitDegrees = ReadFloat(InTable, "primary_orbit_degrees", Parameters.PrimaryOrbitDegrees);
    Parameters.PrimaryAlbedo = ReadFloat(InTable, "primary_albedo", Parameters.PrimaryAlbedo);
    Parameters.SecondaryMoonEnabled = ReadBool(InTable, "secondary_enabled", Parameters.SecondaryMoonEnabled);
    Parameters.SecondaryOrbitDegrees = ReadFloat(InTable, "secondary_orbit_degrees", Parameters.SecondaryOrbitDegrees);
    Parameters.EarthshineLuminance = ReadFloat(InTable, "earthshine", Parameters.EarthshineLuminance);
    Parameters.IlluminateScene = ReadBool(InTable, "illuminate_scene", Parameters.IlluminateScene);
    Parameters.LightPower = std::clamp(ReadFloat(InTable, "light_power", Parameters.LightPower), 0.0f, 5.0f);
    Parameters.Instances[0].Visible = Parameters.Enabled;
    Parameters.Instances[0].PhaseDays = std::clamp(Parameters.PrimaryPhase, 0.0f, 1.0f) * 29.53f;
    Parameters.Instances[1].Visible = Parameters.SecondaryMoonEnabled;
    if (const auto* Instances = InTable["instances"].as_array())
    {
        for (size_t Index = 0; Index < std::min(Instances->size(), Parameters.Instances.size()); ++Index)
        {
            const auto* Entry = (*Instances)[Index].as_table();
            if (Entry == nullptr) continue;
            auto& Moon = Parameters.Instances[Index];
            Moon.Visible = ReadBool(*Entry, "visible", Moon.Visible);
            Moon.FollowSky = ReadBool(*Entry, "follow_sky", Moon.FollowSky);
            Moon.Body = std::min(5u, ReadUint32(*Entry, "body", Moon.Body));
            Moon.PhaseDays = std::clamp(ReadFloat(*Entry, "phase_days", Moon.PhaseDays), 0.0f, 29.53f);
            Moon.AzimuthDegrees = std::clamp(ReadFloat(*Entry, "azimuth_degrees", Moon.AzimuthDegrees), -180.0f, 180.0f);
            Moon.ElevationDegrees = std::clamp(ReadFloat(*Entry, "elevation_degrees", Moon.ElevationDegrees), -90.0f, 90.0f);
            Moon.RollDegrees = std::clamp(ReadFloat(*Entry, "roll_degrees", Moon.RollDegrees), -180.0f, 180.0f);
            Moon.PitchDegrees = std::clamp(ReadFloat(*Entry, "pitch_degrees", Moon.PitchDegrees), -180.0f, 180.0f);
            Moon.SpinDegrees = std::clamp(ReadFloat(*Entry, "spin_degrees", Moon.SpinDegrees), -180.0f, 180.0f);
            Moon.AngularDiameterDegrees = std::clamp(ReadFloat(*Entry, "angular_diameter_degrees", Moon.AngularDiameterDegrees), 0.1f, 180.0f);
            Moon.Brightness = std::clamp(ReadFloat(*Entry, "brightness", Moon.Brightness), 0.0f, 5.0f);
            Moon.Glow = std::clamp(ReadFloat(*Entry, "glow", Moon.Glow), 0.0f, 3.0f);
        }
    }
    Parameters.AdvanceRevision();
}

template <>
void PlanetRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("follow_sky",Parameters.FollowSky);
    WriteFloat3(OutTable,"surface_tint",Parameters.SurfaceTint);
    OutTable.insert_or_assign("azimuth_degrees",double(Parameters.AzimuthDegrees));
    OutTable.insert_or_assign("elevation_degrees",double(Parameters.ElevationDegrees));
    OutTable.insert_or_assign("spin_degrees",double(Parameters.SpinDegrees));
    OutTable.insert_or_assign("tilt_degrees",double(Parameters.TiltDegrees));
    OutTable.insert_or_assign("roll_degrees",double(Parameters.RollDegrees));
    OutTable.insert_or_assign("band_contrast",double(Parameters.BandContrast));
    OutTable.insert_or_assign("surface_detail",double(Parameters.SurfaceDetail));
    OutTable.insert_or_assign("cloud_veil",double(Parameters.CloudVeil));
    OutTable.insert_or_assign("atmosphere_height",double(Parameters.AtmosphereHeight));
    OutTable.insert_or_assign("limb_glow",double(Parameters.LimbGlow));
    OutTable.insert_or_assign("ring_inner_radius",double(Parameters.RingInnerRadius));
    OutTable.insert_or_assign("ring_outer_radius",double(Parameters.RingOuterRadius));
    OutTable.insert_or_assign("ring_optical_depth",double(Parameters.RingOpticalDepth));

    OutTable.insert_or_assign("body", static_cast<int64_t>(Parameters.Body));
    OutTable.insert_or_assign("apparent_diameter_deg", static_cast<double>(Parameters.ApparentDiameterDegrees));
    OutTable.insert_or_assign("brightness", static_cast<double>(Parameters.Brightness));
    OutTable.insert_or_assign("ring_system", Parameters.RingSystem);
    OutTable.insert_or_assign("atmosphere_shell", Parameters.AtmosphereShell);
}

template <>
void PlanetRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled                 = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.Body                    = std::min<uint32_t>(ReadUint32(InTable, "body", Parameters.Body), 3u);
    Parameters.ApparentDiameterDegrees = ReadFloat(InTable, "apparent_diameter_deg", Parameters.ApparentDiameterDegrees);
    Parameters.Brightness              = ReadFloat(InTable, "brightness", Parameters.Brightness);
    Parameters.RingSystem              = ReadBool(InTable, "ring_system", Parameters.RingSystem);
    Parameters.AtmosphereShell         = ReadBool(InTable, "atmosphere_shell", Parameters.AtmosphereShell);
    Parameters.FollowSky = ReadBool(InTable,"follow_sky",Parameters.FollowSky);
    ReadFloat3(InTable,"surface_tint",Parameters.SurfaceTint);
    Parameters.AzimuthDegrees = ReadFloat(InTable,"azimuth_degrees",Parameters.AzimuthDegrees);
    Parameters.ElevationDegrees = ReadFloat(InTable,"elevation_degrees",Parameters.ElevationDegrees);
    Parameters.SpinDegrees = ReadFloat(InTable,"spin_degrees",Parameters.SpinDegrees);
    Parameters.TiltDegrees = ReadFloat(InTable,"tilt_degrees",Parameters.TiltDegrees);
    Parameters.RollDegrees = ReadFloat(InTable,"roll_degrees",Parameters.RollDegrees);
    Parameters.BandContrast = ReadFloat(InTable,"band_contrast",Parameters.BandContrast);
    Parameters.SurfaceDetail = ReadFloat(InTable,"surface_detail",Parameters.SurfaceDetail);
    Parameters.CloudVeil = ReadFloat(InTable,"cloud_veil",Parameters.CloudVeil);
    Parameters.AtmosphereHeight = ReadFloat(InTable,"atmosphere_height",Parameters.AtmosphereHeight);
    Parameters.LimbGlow = ReadFloat(InTable,"limb_glow",Parameters.LimbGlow);
    Parameters.RingInnerRadius = ReadFloat(InTable,"ring_inner_radius",Parameters.RingInnerRadius);
    Parameters.RingOuterRadius = ReadFloat(InTable,"ring_outer_radius",Parameters.RingOuterRadius);
    Parameters.RingOpticalDepth = ReadFloat(InTable,"ring_optical_depth",Parameters.RingOpticalDepth);
    Parameters.AdvanceRevision();
}

template <>
void FogRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("aerial_only", Parameters.AerialOnly);
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("local_volume", Parameters.LocalVolume);
    OutTable.insert_or_assign("base_density", static_cast<double>(Parameters.BaseDensity));
    OutTable.insert_or_assign("height_falloff", static_cast<double>(Parameters.HeightFalloff));
    OutTable.insert_or_assign("base_altitude_m", static_cast<double>(Parameters.BaseAltitudeMetres));
    OutTable.insert_or_assign("anisotropy", static_cast<double>(Parameters.PhaseAnisotropy));
    OutTable.insert_or_assign("start_distance_m", static_cast<double>(Parameters.StartDistanceMetres));
    OutTable.insert_or_assign("froxel_grid", Parameters.VolumetricFroxelGrid);
    OutTable.insert_or_assign("ray_shadows", Parameters.ReceiveRayShadow);
    WriteFloat3(OutTable, "tint_linear", Parameters.TintLinear);
    OutTable.insert_or_assign("sun_scatter", static_cast<double>(Parameters.SunScatter));
    OutTable.insert_or_assign("aerial_enabled", Parameters.AerialEnabled);
    OutTable.insert_or_assign("aerial_density", static_cast<double>(Parameters.AerialDensity));
    OutTable.insert_or_assign("aerial_start_metres", static_cast<double>(Parameters.AerialStartMetres));
    OutTable.insert_or_assign("aerial_mie_blend", static_cast<double>(Parameters.AerialMieBlend));
    OutTable.insert_or_assign("bounds_shape", static_cast<int64_t>(Parameters.BoundsShape));
    WriteFloat3(OutTable, "centre_metres", Parameters.CentreMetres);
    WriteFloat3(OutTable, "half_extents_metres", Parameters.HalfExtentsMetres);
    OutTable.insert_or_assign("density_falloff", Parameters.DensityFalloff);
    OutTable.insert_or_assign("edge_softness", static_cast<double>(Parameters.EdgeSoftness));
    OutTable.insert_or_assign("local_density", static_cast<double>(Parameters.LocalDensity));
    OutTable.insert_or_assign("heterogeneity", static_cast<double>(Parameters.Heterogeneity));
    OutTable.insert_or_assign("noise_scale_metres", static_cast<double>(Parameters.NoiseScaleMetres));
    OutTable.insert_or_assign("independent_wind_mps", static_cast<double>(Parameters.IndependentWindMps));
    OutTable.insert_or_assign("follow_wind", Parameters.FollowWind);
    WriteFloat3(OutTable, "albedo", Parameters.Albedo);
    OutTable.insert_or_assign("absorption", static_cast<double>(Parameters.Absorption));
    OutTable.insert_or_assign("self_shadow", Parameters.SelfShadow);
    OutTable.insert_or_assign("local_samples", static_cast<int64_t>(Parameters.LocalSamples));
}

template <>
void FogRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.AerialOnly = ReadBool(InTable, "aerial_only", Parameters.AerialOnly);
    Parameters.Enabled       = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.LocalVolume   = ReadBool(InTable, "local_volume", Parameters.LocalVolume);
    Parameters.BaseDensity   = ReadFloat(InTable, "base_density", Parameters.BaseDensity);
    Parameters.HeightFalloff = ReadFloat(InTable, "height_falloff", Parameters.HeightFalloff);
    Parameters.BaseAltitudeMetres = ReadFloat(InTable, "base_altitude_m", Parameters.BaseAltitudeMetres);
    Parameters.PhaseAnisotropy = ReadFloat(InTable, "anisotropy", Parameters.PhaseAnisotropy);
    Parameters.StartDistanceMetres = ReadFloat(InTable, "start_distance_m", Parameters.StartDistanceMetres);
    Parameters.VolumetricFroxelGrid = ReadBool(InTable, "froxel_grid", Parameters.VolumetricFroxelGrid);
    Parameters.ReceiveRayShadow = ReadBool(InTable, "ray_shadows", Parameters.ReceiveRayShadow);
    ReadFloat3(InTable, "tint_linear", Parameters.TintLinear);
    Parameters.SunScatter = ReadFloat(InTable, "sun_scatter", Parameters.SunScatter);
    Parameters.AerialEnabled = ReadBool(InTable, "aerial_enabled", Parameters.AerialEnabled);
    Parameters.AerialDensity = ReadFloat(InTable, "aerial_density", Parameters.AerialDensity);
    Parameters.AerialStartMetres = ReadFloat(InTable, "aerial_start_metres", Parameters.AerialStartMetres);
    Parameters.AerialMieBlend = ReadFloat(InTable, "aerial_mie_blend", Parameters.AerialMieBlend);
    Parameters.BoundsShape = ReadUint32(InTable, "bounds_shape", Parameters.BoundsShape);
    ReadFloat3(InTable, "centre_metres", Parameters.CentreMetres);
    ReadFloat3(InTable, "half_extents_metres", Parameters.HalfExtentsMetres);
    Parameters.DensityFalloff = ReadBool(InTable, "density_falloff", Parameters.DensityFalloff);
    Parameters.EdgeSoftness = ReadFloat(InTable, "edge_softness", Parameters.EdgeSoftness);
    Parameters.LocalDensity = ReadFloat(InTable, "local_density", Parameters.LocalDensity);
    Parameters.Heterogeneity = ReadFloat(InTable, "heterogeneity", Parameters.Heterogeneity);
    Parameters.NoiseScaleMetres = ReadFloat(InTable, "noise_scale_metres", Parameters.NoiseScaleMetres);
    Parameters.IndependentWindMps = ReadFloat(InTable, "independent_wind_mps", Parameters.IndependentWindMps);
    Parameters.FollowWind = ReadBool(InTable, "follow_wind", Parameters.FollowWind);
    ReadFloat3(InTable, "albedo", Parameters.Albedo);
    Parameters.Absorption = ReadFloat(InTable, "absorption", Parameters.Absorption);
    Parameters.SelfShadow = ReadBool(InTable, "self_shadow", Parameters.SelfShadow);
    Parameters.LocalSamples = ReadUint32(InTable, "local_samples", Parameters.LocalSamples);
    Parameters.AdvanceRevision();
}

template <>
void LightningRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("strikes_per_minute", static_cast<double>(Parameters.StrikesPerMinute));
    OutTable.insert_or_assign("auto_discharge", Parameters.AutoStormDischarge);
    OutTable.insert_or_assign("branching_factor", static_cast<double>(Parameters.BranchingFactor));
    OutTable.insert_or_assign("peak_current_ka", static_cast<double>(Parameters.PeakCurrentKiloamps));
    OutTable.insert_or_assign("cloud_sheet_luminance", static_cast<double>(Parameters.CloudSheetLuminance));
    OutTable.insert_or_assign("acoustic_shockwave", Parameters.AcousticShockwave);
    OutTable.insert_or_assign("triggered_strike_count", static_cast<int64_t>(Parameters.TriggeredStrikeCount));
    OutTable.insert_or_assign("discharge", static_cast<int64_t>(Parameters.Discharge));
    OutTable.insert_or_assign("cloud_source", static_cast<int64_t>(Parameters.CloudSource));
    OutTable.insert_or_assign("distance_metres", static_cast<double>(Parameters.DistanceMetres));
    OutTable.insert_or_assign("minimum_interval_seconds", static_cast<double>(Parameters.MinimumIntervalSeconds));
    OutTable.insert_or_assign("maximum_interval_seconds", static_cast<double>(Parameters.MaximumIntervalSeconds));
    OutTable.insert_or_assign("channel_span_metres", static_cast<double>(Parameters.ChannelSpanMetres));
    OutTable.insert_or_assign("bolt_brightness", static_cast<double>(Parameters.BoltBrightness));
    OutTable.insert_or_assign("cloud_flash_strength", static_cast<double>(Parameters.CloudFlashStrength));
    OutTable.insert_or_assign("flash_radius_metres", static_cast<double>(Parameters.FlashRadiusMetres));
}

template <>
void LightningRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled            = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.StrikesPerMinute   = ReadFloat(InTable, "strikes_per_minute", Parameters.StrikesPerMinute);
    Parameters.AutoStormDischarge = ReadBool(InTable, "auto_discharge", Parameters.AutoStormDischarge);
    Parameters.BranchingFactor = ReadFloat(InTable, "branching_factor", Parameters.BranchingFactor);
    Parameters.PeakCurrentKiloamps = ReadFloat(InTable, "peak_current_ka", Parameters.PeakCurrentKiloamps);
    Parameters.CloudSheetLuminance = ReadFloat(InTable, "cloud_sheet_luminance", Parameters.CloudSheetLuminance);
    Parameters.AcousticShockwave = ReadBool(InTable, "acoustic_shockwave", Parameters.AcousticShockwave);
    Parameters.TriggeredStrikeCount = ReadUint32(InTable, "triggered_strike_count", Parameters.TriggeredStrikeCount);
    Parameters.Discharge = ReadUint32(InTable, "discharge", Parameters.Discharge);
    Parameters.CloudSource = ReadUint32(InTable, "cloud_source", Parameters.CloudSource);
    Parameters.DistanceMetres = ReadFloat(InTable, "distance_metres", Parameters.DistanceMetres);
    Parameters.MinimumIntervalSeconds = ReadFloat(InTable, "minimum_interval_seconds", Parameters.MinimumIntervalSeconds);
    Parameters.MaximumIntervalSeconds = ReadFloat(InTable, "maximum_interval_seconds", Parameters.MaximumIntervalSeconds);
    Parameters.ChannelSpanMetres = ReadFloat(InTable, "channel_span_metres", Parameters.ChannelSpanMetres);
    Parameters.BoltBrightness = ReadFloat(InTable, "bolt_brightness", Parameters.BoltBrightness);
    Parameters.CloudFlashStrength = ReadFloat(InTable, "cloud_flash_strength", Parameters.CloudFlashStrength);
    Parameters.FlashRadiusMetres = ReadFloat(InTable, "flash_radius_metres", Parameters.FlashRadiusMetres);
    Parameters.AdvanceRevision();
}

template <>
void ForceFieldRecord::SerializeConfiguration(toml::table& Table) const
{
    Table.insert_or_assign("enabled",Parameters.Enabled);
    Table.insert_or_assign("visualize",Parameters.Visualize);
    Table.insert_or_assign("include_turbulence",Parameters.IncludeTurbulence);
    Table.insert_or_assign("grid_resolution",int64_t(Parameters.GridResolution));
    Table.insert_or_assign("arrow_seconds",double(Parameters.ArrowSeconds));
    WriteFloat3(Table,"centre_metres",Parameters.CentreMetres);
    WriteFloat3(Table,"half_extents_metres",Parameters.HalfExtentsMetres);
}

template <>
void ForceFieldRecord::DeserializeConfiguration(const toml::table& Table)
{
    Parameters.Enabled = ReadBool(Table,"enabled",Parameters.Enabled);
    Parameters.Visualize = ReadBool(Table,"visualize",Parameters.Visualize);
    Parameters.IncludeTurbulence = ReadBool(Table,"include_turbulence",Parameters.IncludeTurbulence);
    Parameters.GridResolution = std::clamp(ReadUint32(Table,"grid_resolution",Parameters.GridResolution),3u,8u);
    Parameters.ArrowSeconds = std::clamp(ReadFloat(Table,"arrow_seconds",Parameters.ArrowSeconds),.05f,2.0f);
    ReadFloat3(Table,"centre_metres",Parameters.CentreMetres);
    ReadFloat3(Table,"half_extents_metres",Parameters.HalfExtentsMetres);
    for (float& Half : Parameters.HalfExtentsMetres) Half = std::clamp(Half,1.0f,1000.0f);
    Parameters.AdvanceRevision();
}

template <>
void WindRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("direction_deg", static_cast<double>(Parameters.DirectionDegrees));
    OutTable.insert_or_assign("speed_mps", static_cast<double>(Parameters.SpeedMetresPerSecond));
    OutTable.insert_or_assign("turbulence", static_cast<double>(Parameters.CurlNoiseTurbulence));
    OutTable.insert_or_assign("gust_amplitude", static_cast<double>(Parameters.GustAmplitude));
    OutTable.insert_or_assign("curl_frequency_hz", static_cast<double>(Parameters.CurlFrequencyHz));
    OutTable.insert_or_assign("advect_clouds", Parameters.AdvectClouds);
    OutTable.insert_or_assign("advect_plumes", Parameters.AdvectPlumes);
    OutTable.insert_or_assign("advect_particles", Parameters.AdvectParticles);
    OutTable.insert_or_assign("gust_frequency_hz",double(Parameters.GustFrequencyHz));
    OutTable.insert_or_assign("turbulence_scale_metres",double(Parameters.TurbulenceScaleMetres));
    OutTable.insert_or_assign("gust_strength", static_cast<double>(Parameters.GustStrength));
    OutTable.insert_or_assign("shear_per_kilometre", static_cast<double>(Parameters.ShearPerKilometre));
    OutTable.insert_or_assign("veer_degrees_per_kilometre", static_cast<double>(Parameters.VeerDegreesPerKilometre));
}

template <>
void WindRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.DirectionDegrees     = ReadFloat(InTable, "direction_deg", Parameters.DirectionDegrees);
    Parameters.SpeedMetresPerSecond = ReadFloat(InTable, "speed_mps", Parameters.SpeedMetresPerSecond);
    Parameters.CurlNoiseTurbulence  = ReadFloat(InTable, "turbulence", Parameters.CurlNoiseTurbulence);
    Parameters.GustAmplitude = ReadFloat(InTable, "gust_amplitude", Parameters.GustAmplitude);
    Parameters.CurlFrequencyHz = ReadFloat(InTable, "curl_frequency_hz", Parameters.CurlFrequencyHz);
    Parameters.AdvectClouds = ReadBool(InTable, "advect_clouds", Parameters.AdvectClouds);
    Parameters.AdvectPlumes = ReadBool(InTable, "advect_plumes", Parameters.AdvectPlumes);
    Parameters.AdvectParticles = ReadBool(InTable, "advect_particles", Parameters.AdvectParticles);
    Parameters.GustFrequencyHz = ReadFloat(InTable,"gust_frequency_hz",Parameters.GustFrequencyHz);
    Parameters.TurbulenceScaleMetres = ReadFloat(InTable,"turbulence_scale_metres",Parameters.TurbulenceScaleMetres);
    Parameters.GustStrength = ReadFloat(InTable, "gust_strength", Parameters.GustStrength);
    Parameters.ShearPerKilometre = ReadFloat(InTable, "shear_per_kilometre", Parameters.ShearPerKilometre);
    Parameters.VeerDegreesPerKilometre = ReadFloat(InTable, "veer_degrees_per_kilometre", Parameters.VeerDegreesPerKilometre);
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

namespace
{

// 📝 Rounded to 0.1 mm / 0.0001: the record keeps floats, and the level file should read the figures the inspector shows.
void WriteRoundedFloat3(toml::table& OutTable, std::string_view Key, const float Values[3])
{
    toml::array Array;
    for (int Axis = 0; Axis < 3; ++Axis) Array.push_back(std::round(static_cast<double>(Values[Axis]) * 1.0e4) / 1.0e4);
    OutTable.insert_or_assign(Key, std::move(Array));
}

} // namespace

template <>
void GeometryRecord::SerializeConfiguration(toml::table& OutTable) const
{
    toml::array Stems;
    for (const std::string& Stem : Parameters.OccupantStems) Stems.push_back(Stem);
    OutTable.insert_or_assign("occupants", std::move(Stems));
    if (Parameters.OccupantFirst != 0) OutTable.insert_or_assign("occupant_first", static_cast<int64_t>(Parameters.OccupantFirst));
    if (Parameters.OccupantCount != 0) OutTable.insert_or_assign("occupant_count", static_cast<int64_t>(Parameters.OccupantCount));
    if (Parameters.BaseCopies != 0) OutTable.insert_or_assign("base_copies", static_cast<int64_t>(Parameters.BaseCopies));
    if (!Parameters.ClaimsArchive) OutTable.insert_or_assign("claims_archive", false);
    if (!Parameters.ShapeName.empty()) OutTable.insert_or_assign("shape", Parameters.ShapeName);
    if (Parameters.ArchiveOriginAuthored)
    {
        WriteRoundedFloat3(OutTable, "archive_origin_m", Parameters.ArchiveOriginMetres);
        OutTable.insert_or_assign("archive_yaw_deg", std::round(static_cast<double>(Parameters.ArchiveYawDegrees) * 1.0e4) / 1.0e4);
    }
    OutTable.insert_or_assign("group", Parameters.GroupName);
    OutTable.insert_or_assign("glyph", Parameters.GlyphName);
    if (!Parameters.Notes.empty()) OutTable.insert_or_assign("notes", Parameters.Notes);
    OutTable.insert_or_assign("visible", Parameters.Visible);
    OutTable.insert_or_assign("dynamic", Parameters.Dynamic);
    OutTable.insert_or_assign("cast_shadows", Parameters.CastShadows);
    OutTable.insert_or_assign("global_illumination", Parameters.GlobalIllumination);
    OutTable.insert_or_assign("physics", Parameters.Physics);
    OutTable.insert_or_assign("locked", Parameters.Locked);
    WriteRoundedFloat3(OutTable, "position_m", Parameters.Spatial.PositionMetres);
    WriteRoundedFloat3(OutTable, "orientation_deg", Parameters.Spatial.OrientationDegrees);
    WriteRoundedFloat3(OutTable, "scale", Parameters.Spatial.Scale);
    OutTable.insert_or_assign("fracture", Parameters.Fracture.Enabled);
    if (Parameters.Fracture.Enabled)
    {
        OutTable.insert_or_assign("voronoi_cells", static_cast<int64_t>(Parameters.Fracture.VoronoiCellCount));
        OutTable.insert_or_assign("impulse_threshold_ns", static_cast<double>(Parameters.Fracture.ImpulseThresholdNs));
    }
    toml::array Stack;
    for (const ArrayModifierConfiguration& Modifier : Parameters.Modifiers)
    {
        toml::table Entry;
        Entry.insert_or_assign("type", "Array");
        Entry.insert_or_assign("label", Modifier.Label);
        Entry.insert_or_assign("enabled", Modifier.Enabled);
        Entry.insert_or_assign("expanded", Modifier.Expanded);
        Entry.insert_or_assign("count", static_cast<int64_t>(Modifier.Count));
        Entry.insert_or_assign("relative_offset", Modifier.RelativeOffset);
        WriteRoundedFloat3(Entry, "relative_factor", Modifier.RelativeFactor);
        Entry.insert_or_assign("constant_offset", Modifier.ConstantOffset);
        WriteRoundedFloat3(Entry, "constant_m", Modifier.ConstantMetres);
        Entry.insert_or_assign("rotation_step_deg", std::round(static_cast<double>(Modifier.RotationStepDegrees) * 1.0e4) / 1.0e4);
        Entry.insert_or_assign("radial_pivot", Modifier.RadialPivot);
        Stack.push_back(std::move(Entry));
    }
    if (!Stack.empty()) OutTable.insert_or_assign("modifiers", std::move(Stack));
    // 📝 The surfaces the object owns: [[surfaces]] family = n, albedo = [r, g, b], roughness = …, as the shape archives name them.
    toml::array Surfaces;
    for (const SurfaceSlotConfiguration& Slot : Parameters.SurfaceSlots)
    {
        toml::table Entry;
        const auto& Channel = Slot.Channels;
        auto Rounded = [](float Scalar) { return std::round(static_cast<double>(Scalar) * 1.0e5) / 1.0e5; };
        Entry.insert_or_assign("family", static_cast<int64_t>(Slot.Family));
        Entry.insert_or_assign("albedo", toml::array{Rounded(Channel[0]), Rounded(Channel[1]), Rounded(Channel[2])});
        Entry.insert_or_assign("roughness", Rounded(Channel[3]));
        Entry.insert_or_assign("incandescence", toml::array{Rounded(Channel[4]), Rounded(Channel[5]), Rounded(Channel[6])});
        Entry.insert_or_assign("metalness", Rounded(Channel[7]));
        Entry.insert_or_assign("coat", Rounded(Channel[8]));
        Entry.insert_or_assign("coat_roughness", Rounded(Channel[9]));
        Entry.insert_or_assign("specular", Rounded(Channel[10]));
        Entry.insert_or_assign("transmission", Rounded(Channel[11]));
        Surfaces.push_back(std::move(Entry));
    }
    if (!Surfaces.empty()) OutTable.insert_or_assign("surfaces", std::move(Surfaces));
}

template <>
void GeometryRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.OccupantStems.clear();
    if (const toml::array* Stems = InTable["occupants"].as_array())
        for (const toml::node& Stem : *Stems)
            if (const auto Text = Stem.value<std::string>()) Parameters.OccupantStems.push_back(*Text);
    Parameters.OccupantFirst      = ReadUint32(InTable, "occupant_first", 0u);
    Parameters.OccupantCount      = ReadUint32(InTable, "occupant_count", 0u);
    Parameters.BaseCopies         = ReadUint32(InTable, "base_copies", 0u);
    Parameters.ClaimsArchive      = ReadBool(InTable, "claims_archive", true);
    Parameters.ShapeName          = InTable["shape"].value_or(std::string());
    Parameters.ArchiveOriginAuthored = InTable.contains("archive_origin_m");
    if (Parameters.ArchiveOriginAuthored) ReadFloat3(InTable, "archive_origin_m", Parameters.ArchiveOriginMetres);
    Parameters.ArchiveYawDegrees  = ReadFloat(InTable, "archive_yaw_deg", 0.0f);
    Parameters.GroupName          = InTable["group"].value_or(std::string("Geometry"));
    Parameters.GlyphName          = InTable["glyph"].value_or(std::string("Box"));
    Parameters.Notes              = InTable["notes"].value_or(std::string());
    Parameters.Visible            = ReadBool(InTable, "visible", Parameters.Visible);
    Parameters.Dynamic            = ReadBool(InTable, "dynamic", Parameters.Dynamic);
    Parameters.CastShadows        = ReadBool(InTable, "cast_shadows", Parameters.CastShadows);
    Parameters.GlobalIllumination = ReadBool(InTable, "global_illumination", Parameters.GlobalIllumination);
    Parameters.Physics            = ReadBool(InTable, "physics", Parameters.Physics);
    Parameters.Locked             = ReadBool(InTable, "locked", Parameters.Locked);
    DeserializeSpatialSubtable(InTable, Parameters.Spatial);
    Parameters.Fracture.Enabled            = ReadBool(InTable, "fracture", false);
    Parameters.Fracture.VoronoiCellCount   = ReadUint32(InTable, "voronoi_cells", Parameters.Fracture.VoronoiCellCount);
    Parameters.Fracture.ImpulseThresholdNs = ReadFloat(InTable, "impulse_threshold_ns", Parameters.Fracture.ImpulseThresholdNs);
    Parameters.Modifiers.clear();
    if (const toml::array* Stack = InTable["modifiers"].as_array())
    {
        for (const toml::node& Node : *Stack)
        {
            const toml::table* Entry = Node.as_table();
            if (Entry == nullptr || (*Entry)["type"].value_or(std::string("Array")) != "Array") continue;
            ArrayModifierConfiguration Modifier;
            Modifier.Label               = (*Entry)["label"].value_or(std::string("Array"));
            Modifier.Enabled             = ReadBool(*Entry, "enabled", Modifier.Enabled);
            Modifier.Expanded            = ReadBool(*Entry, "expanded", Modifier.Expanded);
            Modifier.Count               = std::max(1u, ReadUint32(*Entry, "count", Modifier.Count));
            Modifier.RelativeOffset      = ReadBool(*Entry, "relative_offset", Modifier.RelativeOffset);
            ReadFloat3(*Entry, "relative_factor", Modifier.RelativeFactor);
            Modifier.ConstantOffset      = ReadBool(*Entry, "constant_offset", Modifier.ConstantOffset);
            ReadFloat3(*Entry, "constant_m", Modifier.ConstantMetres);
            Modifier.RotationStepDegrees = ReadFloat(*Entry, "rotation_step_deg", Modifier.RotationStepDegrees);
            Modifier.RadialPivot         = ReadBool(*Entry, "radial_pivot", Modifier.RadialPivot);
            Parameters.Modifiers.push_back(std::move(Modifier));
        }
    }
    Parameters.SurfaceSlots.clear();
    if (const toml::array* Surfaces = InTable["surfaces"].as_array())
    {
        for (const toml::node& Node : *Surfaces)
        {
            const toml::table* Entry = Node.as_table();
            if (Entry == nullptr) continue;
            SurfaceSlotConfiguration Slot;
            Slot.Family = ReadUint32(*Entry, "family", 0u);
            auto Read3 = [&](const char* Key, size_t First) {
                if (const toml::array* Values = (*Entry)[Key].as_array())
                    for (size_t Index = 0; Index < 3 && Index < Values->size(); ++Index)
                        Slot.Channels[First + Index] = static_cast<float>((*Values)[Index].value_or(0.0));
            };
            Read3("albedo", 0);
            Read3("incandescence", 4);
            Slot.Channels[3]  = ReadFloat(*Entry, "roughness", 0.5f);
            Slot.Channels[7]  = ReadFloat(*Entry, "metalness", 0.0f);
            Slot.Channels[8]  = ReadFloat(*Entry, "coat", 0.0f);
            Slot.Channels[9]  = ReadFloat(*Entry, "coat_roughness", 0.0f);
            Slot.Channels[10] = ReadFloat(*Entry, "specular", 0.04f);
            Slot.Channels[11] = ReadFloat(*Entry, "transmission", 0.0f);
            Parameters.SurfaceSlots.push_back(Slot);
        }
    }
    ConfigureEditorPresentation(Parameters.GlyphName.c_str(), Parameters.GroupName.c_str(), "Geometry", "");
    Parameters.AdvanceRevision();
}

namespace
{

void SerializeTyreCornerTable(toml::table& OutTable, const TyreCornerConfiguration& Corner)
{
    OutTable.insert_or_assign("corner_code", Corner.CornerCode);
    OutTable.insert_or_assign("set_identifier", Corner.SetIdentifier);
    OutTable.insert_or_assign("compound_index", static_cast<int64_t>(Corner.CompoundIndex));
    OutTable.insert_or_assign("enclosed", Corner.EnclosedCondition);
    OutTable.insert_or_assign("punctured", Corner.PuncturedCondition);
    OutTable.insert_or_assign("cold_pressure_kpa", static_cast<double>(Corner.ColdPressureKpa));
    OutTable.insert_or_assign("hot_pressure_kpa", static_cast<double>(Corner.HotPressureKpa));
    OutTable.insert_or_assign("surface_temperature_c", static_cast<double>(Corner.SurfaceTemperatureC));
    OutTable.insert_or_assign("belt_temperature_c", static_cast<double>(Corner.BeltTemperatureC));
    OutTable.insert_or_assign("gas_temperature_c", static_cast<double>(Corner.GasTemperatureC));
    OutTable.insert_or_assign("core_temperature_c", static_cast<double>(Corner.CoreTemperatureC));
    OutTable.insert_or_assign("wear_percent", static_cast<double>(Corner.WearPercent));
    OutTable.insert_or_assign("stint_laps", static_cast<int64_t>(Corner.StintLaps));
    OutTable.insert_or_assign("tread_depth_mm", static_cast<double>(Corner.TreadDepthMm));
    OutTable.insert_or_assign("loaded_radius_mm", static_cast<double>(Corner.LoadedRadiusMm));
    OutTable.insert_or_assign("section_width_mm", static_cast<double>(Corner.SectionWidthMm));
    OutTable.insert_or_assign("contact_patch_load_kn", static_cast<double>(Corner.ContactPatchLoadKn));
    OutTable.insert_or_assign("pacejka_cx", static_cast<double>(Corner.PacejkaLongitudinalShape));
    OutTable.insert_or_assign("pacejka_cy", static_cast<double>(Corner.PacejkaLateralShape));
    OutTable.insert_or_assign("load_sensitivity", static_cast<double>(Corner.LoadSensitivity));
    OutTable.insert_or_assign("detail_unit_display_imperial", Corner.UnitDisplayImperial);
    OutTable.insert_or_assign("detail_optimal_surface_temp_c", static_cast<double>(Corner.OptimalSurfaceTempC));
    OutTable.insert_or_assign("detail_thermal_window_half_width_c", static_cast<double>(Corner.ThermalWindowHalfWidthC));

}

void DeserializeTyreCornerTable(const toml::table& InTable, TyreCornerConfiguration& Corner)
{
    if (const auto Val = InTable["corner_code"].value<std::string>()) Corner.CornerCode = *Val;
    if (const auto Val = InTable["set_identifier"].value<std::string>()) Corner.SetIdentifier = *Val;
    Corner.CompoundIndex            = std::clamp(ReadInt(InTable, "compound_index", Corner.CompoundIndex), 0, 4);
    Corner.EnclosedCondition        = ReadBool(InTable, "enclosed", Corner.EnclosedCondition);
    Corner.PuncturedCondition       = ReadBool(InTable, "punctured", Corner.PuncturedCondition);
    Corner.ColdPressureKpa          = ReadFloat(InTable, "cold_pressure_kpa", Corner.ColdPressureKpa);
    Corner.HotPressureKpa           = ReadFloat(InTable, "hot_pressure_kpa", Corner.HotPressureKpa);
    Corner.SurfaceTemperatureC      = ReadFloat(InTable, "surface_temperature_c", Corner.SurfaceTemperatureC);
    Corner.BeltTemperatureC         = ReadFloat(InTable, "belt_temperature_c", Corner.BeltTemperatureC);
    Corner.GasTemperatureC          = ReadFloat(InTable, "gas_temperature_c", Corner.GasTemperatureC);
    Corner.CoreTemperatureC         = ReadFloat(InTable, "core_temperature_c", Corner.CoreTemperatureC);
    Corner.WearPercent              = ReadFloat(InTable, "wear_percent", Corner.WearPercent);
    Corner.StintLaps                = std::max(0, ReadInt(InTable, "stint_laps", Corner.StintLaps));
    Corner.TreadDepthMm             = ReadFloat(InTable, "tread_depth_mm", Corner.TreadDepthMm);
    Corner.LoadedRadiusMm           = ReadFloat(InTable, "loaded_radius_mm", Corner.LoadedRadiusMm);
    Corner.SectionWidthMm           = ReadFloat(InTable, "section_width_mm", Corner.SectionWidthMm);
    Corner.ContactPatchLoadKn       = ReadFloat(InTable, "contact_patch_load_kn", Corner.ContactPatchLoadKn);
    Corner.PacejkaLongitudinalShape = ReadFloat(InTable, "pacejka_cx", Corner.PacejkaLongitudinalShape);
    Corner.PacejkaLateralShape      = ReadFloat(InTable, "pacejka_cy", Corner.PacejkaLateralShape);
    Corner.LoadSensitivity          = ReadFloat(InTable, "load_sensitivity", Corner.LoadSensitivity);
    Corner.UnitDisplayImperial = ReadBool(InTable, "detail_unit_display_imperial", Corner.UnitDisplayImperial);
    Corner.OptimalSurfaceTempC = ReadFloat(InTable, "detail_optimal_surface_temp_c", Corner.OptimalSurfaceTempC);
    Corner.ThermalWindowHalfWidthC = ReadFloat(InTable, "detail_thermal_window_half_width_c", Corner.ThermalWindowHalfWidthC);

}

void WriteCurvePointsArray(toml::table& OutTable, std::string_view Key, const std::vector<VehicleCurveControlPoint>& Points)
{
    toml::array Outer;
    for (const VehicleCurveControlPoint& Pt : Points)
    {
        toml::array Pair;
        Pair.push_back(static_cast<double>(Pt.X));
        Pair.push_back(static_cast<double>(Pt.Y));
        Outer.push_back(std::move(Pair));
    }
    OutTable.insert_or_assign(Key, std::move(Outer));
}

void ReadCurvePointsArray(const toml::table& InTable, std::string_view Key, std::vector<VehicleCurveControlPoint>& OutPoints)
{
    const toml::array* Outer = InTable[Key].as_array();
    if (Outer == nullptr || Outer->size() < 2u) return;
    std::vector<VehicleCurveControlPoint> Parsed;
    Parsed.reserve(Outer->size());
    for (const toml::node& Entry : *Outer)
    {
        const toml::array* Pair = Entry.as_array();
        if (Pair == nullptr || Pair->size() < 2u) continue;
        const auto X = (*Pair)[0].value<double>();
        const auto Y = (*Pair)[1].value<double>();
        if (X && Y && std::isfinite(*X) && std::isfinite(*Y))
        {
            Parsed.push_back({static_cast<float>(*X), static_cast<float>(*Y)});
        }
    }
    if (Parsed.size() >= 2u)
    {
        OutPoints = std::move(Parsed);
    }
}

} // namespace

TyreRecord::TyreRecord(RecordClassification Classification, std::string_view TypeIdentifier,
                       std::string DisplayName, const char* GlyphIdentifier, const char* GroupName, const char* AccentHex)
    : SceneRecordBase(Classification, TypeIdentifier, std::move(DisplayName))
{
    ConfigureEditorPresentation(GlyphIdentifier, GroupName, "Tyre", AccentHex);
    RefreshInspector();
}

void TyreRecord::RefreshInspector()
{
#if FRONTIER_EDITOR
    InspectorRuntime::Instance().BindInspector(QueryToken(), std::make_unique<TyreInspector>(*Corner));
#endif
}

void TyreRecord::AttachCorner(TyreCornerConfiguration& Target)
{
    if (Corner == &Target) return;
    Target = *Corner;
    Corner = &Target;
    RefreshInspector();
}

void TyreRecord::DetachCorner()
{
    if (Corner == &LocalCorner) return;
    LocalCorner = *Corner;
    Corner = &LocalCorner;
    RefreshInspector();
}

void TyreRecord::SerializeConfiguration(toml::table& OutTable) const
{
    SerializeTyreCornerTable(OutTable, *Corner);
}

void TyreRecord::DeserializeConfiguration(const toml::table& InTable)
{
    DeserializeTyreCornerTable(InTable, *Corner);
    Corner->AdvanceRevision();
}

template <>
void VehicleRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("identity", Parameters.VehicleName);
    OutTable.insert_or_assign("maker", Parameters.MakerName);
    OutTable.insert_or_assign("classification", Parameters.ClassificationCode);
    OutTable.insert_or_assign("kerb_mass_kg", static_cast<double>(Parameters.KerbMassKg));
    OutTable.insert_or_assign("wheelbase_mm", static_cast<double>(Parameters.WheelbaseMm));
    OutTable.insert_or_assign("track_front_mm", static_cast<double>(Parameters.TrackFrontMm));
    OutTable.insert_or_assign("track_rear_mm", static_cast<double>(Parameters.TrackRearMm));
    OutTable.insert_or_assign("centre_of_gravity_height_mm", static_cast<double>(Parameters.CentreOfGravityHeightMm));
    OutTable.insert_or_assign("ride_height_mm", static_cast<double>(Parameters.RideHeightMm));
    OutTable.insert_or_assign("fuel_capacity_l", static_cast<double>(Parameters.FuelCapacityLitres));
    OutTable.insert_or_assign("fuel_load_l", static_cast<double>(Parameters.FuelLoadLitres));
    OutTable.insert_or_assign("rear_mass_share_percent", static_cast<double>(Parameters.StaticRearMassPercent));
    OutTable.insert_or_assign("drive_layout_index", static_cast<int64_t>(Parameters.DriveLayoutIndex));

    OutTable.insert_or_assign("displacement_l", static_cast<double>(Parameters.DisplacementLitres));
    OutTable.insert_or_assign("idle_speed_rpm", static_cast<double>(Parameters.IdleSpeedRpm));
    OutTable.insert_or_assign("redline_rpm", static_cast<double>(Parameters.RedlineRpm));
    OutTable.insert_or_assign("rated_torque_nm", static_cast<double>(Parameters.RatedTorqueNm));
    OutTable.insert_or_assign("engine_inertia_kgm2", static_cast<double>(Parameters.EngineInertiaKgm2));
    OutTable.insert_or_assign("induction_mode_index", static_cast<int64_t>(Parameters.InductionModeIndex));
    OutTable.insert_or_assign("race_tune", Parameters.RaceTuneEnabled);
    OutTable.insert_or_assign("supercharger_type_index", static_cast<int64_t>(Parameters.SuperchargerTypeIndex));
    OutTable.insert_or_assign("supercharger_drive_ratio", static_cast<double>(Parameters.SuperchargerDriveRatio));
    OutTable.insert_or_assign("supercharger_displacement_cc", static_cast<double>(Parameters.SuperchargerDisplacementCc));
    OutTable.insert_or_assign("supercharger_max_boost_bar", static_cast<double>(Parameters.SuperchargerMaxBoostBar));
    OutTable.insert_or_assign("supercharger_parasitic_nm", static_cast<double>(Parameters.SuperchargerPeakParasiticNm));
    OutTable.insert_or_assign("turbo_boost_bar", static_cast<double>(Parameters.TurboBoostBar));

    OutTable.insert_or_assign("forward_gear_count", static_cast<int64_t>(Parameters.ForwardGearCount));
    OutTable.insert_or_assign("final_drive_ratio", static_cast<double>(Parameters.FinalDriveRatio));
    OutTable.insert_or_assign("differential_type_index", static_cast<int64_t>(Parameters.DifferentialTypeIndex));
    OutTable.insert_or_assign("lsd_preload_nm", static_cast<double>(Parameters.LsdPreloadTorqueNm));
    OutTable.insert_or_assign("torsen_bias_ratio", static_cast<double>(Parameters.TorsenBiasRatio));

    OutTable.insert_or_assign("front_spring_kn_m", static_cast<double>(Parameters.FrontSpringRateKnpm));
    OutTable.insert_or_assign("rear_spring_kn_m", static_cast<double>(Parameters.RearSpringRateKnpm));
    OutTable.insert_or_assign("ackermann_percent", static_cast<double>(Parameters.AckermannPercent));
    OutTable.insert_or_assign("traction_control", Parameters.TractionControlEnabled);
    OutTable.insert_or_assign("stability_control", Parameters.StabilityControlEnabled);
    OutTable.insert_or_assign("anti_lock_brakes", Parameters.AntiLockBrakesEnabled);

    OutTable.insert_or_assign("soft_body_formulation_index", static_cast<int64_t>(Parameters.SoftBodyFormulationIndex));
    OutTable.insert_or_assign("slip_solver_mode_index", static_cast<int64_t>(Parameters.SlipSolverModeIndex));
    OutTable.insert_or_assign("fem_substeps", static_cast<int64_t>(Parameters.FemSubsteps));
    OutTable.insert_or_assign("fem_youngs_modulus_mpa", static_cast<double>(Parameters.FemYoungsModulusMpa));
    OutTable.insert_or_assign("fem_poisson_ratio", static_cast<double>(Parameters.FemPoissonRatio));

    OutTable.insert_or_assign("oil_viscosity_grade_index", static_cast<int64_t>(Parameters.OilViscosityGradeIndex));
    OutTable.insert_or_assign("coolant_target_c", static_cast<double>(Parameters.CoolantTargetC));
    OutTable.insert_or_assign("drag_coefficient", static_cast<double>(Parameters.DragCoefficient));
    OutTable.insert_or_assign("downforce_at_200_kmh_n", static_cast<double>(Parameters.DownforceAt200KmhN));
    OutTable.insert_or_assign("venturi_peak_gain", static_cast<double>(Parameters.VenturiPeakGain));
    OutTable.insert_or_assign("line_lock_front_only", Parameters.LineLockFrontOnly);

    OutTable.insert_or_assign("outer_tick_hz", static_cast<int64_t>(Parameters.OuterTickHz));
    OutTable.insert_or_assign("substeps_per_outer_tick", static_cast<int64_t>(Parameters.SubstepsPerOuterTick));
    OutTable.insert_or_assign("stationarity_hold", Parameters.StationarityHoldEnabled);
    OutTable.insert_or_assign("force_zone_coupling", Parameters.ForceZoneCouplingEnabled);

    OutTable.insert_or_assign("curve_smooth_monotone", Parameters.CurveSmoothMonotone);
    WriteCurvePointsArray(OutTable, "engine_torque_points", Parameters.EngineTorqueCurve);
    WriteCurvePointsArray(OutTable, "brake_torque_points", Parameters.BrakeTorqueCurve);
    WriteCurvePointsArray(OutTable, "coolant_temperature_points", Parameters.CoolantTempCurve);

    OutTable.insert_or_assign("detail_unit_display_imperial", Parameters.UnitDisplayImperial);
    OutTable.insert_or_assign("detail_inspector_mode_index", static_cast<int64_t>(Parameters.InspectorModeIndex));
    OutTable.insert_or_assign("detail_fuel_reserve_percent", static_cast<double>(Parameters.FuelReservePercent));
    OutTable.insert_or_assign("detail_fuel_burn_kg_per_hour", static_cast<double>(Parameters.FuelBurnKgPerHour));
    OutTable.insert_or_assign("detail_thermostat_opening_c", static_cast<double>(Parameters.ThermostatOpeningC));
    OutTable.insert_or_assign("detail_supercharger_bypass_parasitic_pct", static_cast<double>(Parameters.SuperchargerBypassParasiticPct));
    OutTable.insert_or_assign("detail_supercharger_bypass_throttle_pct", static_cast<double>(Parameters.SuperchargerBypassThrottlePct));
    OutTable.insert_or_assign("detail_turbo_spool_up_rate", static_cast<double>(Parameters.TurboSpoolUpRate));
    OutTable.insert_or_assign("detail_turbo_spool_down_rate", static_cast<double>(Parameters.TurboSpoolDownRate));
    OutTable.insert_or_assign("detail_torque_gain_per_bar", static_cast<double>(Parameters.TorqueGainPerBar));
    OutTable.insert_or_assign("detail_driveline_efficiency_pct", static_cast<double>(Parameters.DrivelineEfficiencyPct));
    OutTable.insert_or_assign("detail_loaded_wheel_radius_mm", static_cast<double>(Parameters.LoadedWheelRadiusMm));
    OutTable.insert_or_assign("detail_peak_tyre_friction", static_cast<double>(Parameters.PeakTyreFriction));
    OutTable.insert_or_assign("detail_lsd_power_lock_percent", static_cast<double>(Parameters.LsdPowerLockPercent));
    OutTable.insert_or_assign("detail_lsd_coast_lock_percent", static_cast<double>(Parameters.LsdCoastLockPercent));
    OutTable.insert_or_assign("detail_awd_front_torque_percent", static_cast<double>(Parameters.AwdFrontTorquePercent));
    OutTable.insert_or_assign("detail_shift_duration_ms", static_cast<double>(Parameters.ShiftDurationMs));
    OutTable.insert_or_assign("detail_clutch_capacity_nm", static_cast<double>(Parameters.ClutchCapacityNm));
    OutTable.insert_or_assign("detail_progressive_spring_factor", static_cast<double>(Parameters.ProgressiveSpringFactor));
    OutTable.insert_or_assign("detail_front_rest_length_mm", static_cast<double>(Parameters.FrontRestLengthMm));
    OutTable.insert_or_assign("detail_rear_rest_length_mm", static_cast<double>(Parameters.RearRestLengthMm));
    OutTable.insert_or_assign("detail_bump_stop_stiffness_knpm", static_cast<double>(Parameters.BumpStopStiffnessKnpm));
    OutTable.insert_or_assign("detail_front_bump_damping_nsm", static_cast<double>(Parameters.FrontBumpDampingNsm));
    OutTable.insert_or_assign("detail_front_rebound_damping_nsm", static_cast<double>(Parameters.FrontReboundDampingNsm));
    OutTable.insert_or_assign("detail_rear_bump_damping_nsm", static_cast<double>(Parameters.RearBumpDampingNsm));
    OutTable.insert_or_assign("detail_rear_rebound_damping_nsm", static_cast<double>(Parameters.RearReboundDampingNsm));
    OutTable.insert_or_assign("detail_high_speed_damping_ratio", static_cast<double>(Parameters.HighSpeedDampingRatio));
    OutTable.insert_or_assign("detail_damper_knee_velocity_mps", static_cast<double>(Parameters.DamperKneeVelocityMps));
    OutTable.insert_or_assign("detail_front_anti_roll_knpm", static_cast<double>(Parameters.FrontAntiRollKnpm));
    OutTable.insert_or_assign("detail_rear_anti_roll_knpm", static_cast<double>(Parameters.RearAntiRollKnpm));
    OutTable.insert_or_assign("detail_front_static_camber_deg", static_cast<double>(Parameters.FrontStaticCamberDeg));
    OutTable.insert_or_assign("detail_rear_static_camber_deg", static_cast<double>(Parameters.RearStaticCamberDeg));
    OutTable.insert_or_assign("detail_max_inner_steer_deg", static_cast<double>(Parameters.MaxInnerSteerDeg));
    OutTable.insert_or_assign("detail_steering_rack_ratio", static_cast<double>(Parameters.SteeringRackRatio));
    OutTable.insert_or_assign("detail_speed_sensitive_steer_floor_pct", static_cast<double>(Parameters.SpeedSensitiveSteerFloorPct));
    OutTable.insert_or_assign("detail_mechanical_trail_mm", static_cast<double>(Parameters.MechanicalTrailMm));
    OutTable.insert_or_assign("detail_launch_control_enabled", Parameters.LaunchControlEnabled);
    OutTable.insert_or_assign("detail_drs_aid_enabled", Parameters.DrsAidEnabled);
    OutTable.insert_or_assign("detail_rev_limiter_enabled", Parameters.RevLimiterEnabled);
    OutTable.insert_or_assign("detail_telemetry_enabled", Parameters.TelemetryEnabled);
    OutTable.insert_or_assign("detail_esc_yaw_gain", static_cast<double>(Parameters.EscYawGain));
    OutTable.insert_or_assign("detail_tcs_slip_threshold_pct", static_cast<double>(Parameters.TcsSlipThresholdPct));
    OutTable.insert_or_assign("detail_abs_slip_threshold_pct", static_cast<double>(Parameters.AbsSlipThresholdPct));
    OutTable.insert_or_assign("detail_circumferential_node_count", static_cast<int64_t>(Parameters.CircumferentialNodeCount));
    OutTable.insert_or_assign("detail_lateral_ring_count", static_cast<int64_t>(Parameters.LateralRingCount));
    OutTable.insert_or_assign("detail_vertical_stiffness_knpm", static_cast<double>(Parameters.VerticalStiffnessKnpm));
    OutTable.insert_or_assign("detail_pressure_stiffness_gain", static_cast<double>(Parameters.PressureStiffnessGain));
    OutTable.insert_or_assign("detail_pneumatic_volume_weight_pct", static_cast<double>(Parameters.PneumaticVolumeWeightPct));
    OutTable.insert_or_assign("detail_xpbd_substeps", static_cast<int64_t>(Parameters.XpbdSubsteps));
    OutTable.insert_or_assign("detail_xpbd_iterations", static_cast<int64_t>(Parameters.XpbdIterations));
    OutTable.insert_or_assign("detail_max_contact_bulge_pct", static_cast<double>(Parameters.MaxContactBulgePct));
    OutTable.insert_or_assign("detail_fem_vertex_mass_kg", static_cast<double>(Parameters.FemVertexMassKg));
    OutTable.insert_or_assign("detail_fem_damping_rate_hz", static_cast<double>(Parameters.FemDampingRateHz));
    OutTable.insert_or_assign("detail_oil_target_c", static_cast<double>(Parameters.OilTargetC));
    OutTable.insert_or_assign("detail_brake_target_c", static_cast<double>(Parameters.BrakeTargetC));
    OutTable.insert_or_assign("detail_ambient_temperature_c", static_cast<double>(Parameters.AmbientTemperatureC));
    OutTable.insert_or_assign("detail_cooling_capacity_kw", static_cast<double>(Parameters.CoolingCapacityKw));
    OutTable.insert_or_assign("detail_coolant_heat_capacity_kj_k", static_cast<double>(Parameters.CoolantHeatCapacityKjK));
    OutTable.insert_or_assign("detail_radiator_airflow_gain", static_cast<double>(Parameters.RadiatorAirflowGain));
    OutTable.insert_or_assign("detail_radiator_fan_threshold_c", static_cast<double>(Parameters.RadiatorFanThresholdC));
    OutTable.insert_or_assign("detail_overheat_derate_start_c", static_cast<double>(Parameters.OverheatDerateStartC));
    OutTable.insert_or_assign("detail_specific_fuel_burn_gkwh", static_cast<double>(Parameters.SpecificFuelBurnGkwh));
    OutTable.insert_or_assign("detail_frontal_area_m2", static_cast<double>(Parameters.FrontalAreaM2));
    OutTable.insert_or_assign("detail_total_brake_torque_nm", static_cast<double>(Parameters.TotalBrakeTorqueNm));
    OutTable.insert_or_assign("detail_brake_balance_front_pct", static_cast<double>(Parameters.BrakeBalanceFrontPct));
    OutTable.insert_or_assign("detail_rolling_resistance", static_cast<double>(Parameters.RollingResistance));
    OutTable.insert_or_assign("detail_aero_balance_front_pct", static_cast<double>(Parameters.AeroBalanceFrontPct));
    OutTable.insert_or_assign("detail_pitch_aero_sensitivity", static_cast<double>(Parameters.PitchAeroSensitivity));
    OutTable.insert_or_assign("detail_drs_drag_reduction_pct", static_cast<double>(Parameters.DrsDragReductionPct));
    OutTable.insert_or_assign("detail_drs_downforce_loss_pct", static_cast<double>(Parameters.DrsDownforceLossPct));
    OutTable.insert_or_assign("detail_drs_activation_speed_kmh", static_cast<double>(Parameters.DrsActivationSpeedKmh));
    OutTable.insert_or_assign("detail_airbrake_extra_cd", static_cast<double>(Parameters.AirbrakeExtraCd));
    OutTable.insert_or_assign("detail_airbrake_downforce_n", static_cast<double>(Parameters.AirbrakeDownforceN));
    OutTable.insert_or_assign("detail_venturi_optimal_height_mm", static_cast<double>(Parameters.VenturiOptimalHeightMm));
    OutTable.insert_or_assign("detail_venturi_choke_height_mm", static_cast<double>(Parameters.VenturiChokeHeightMm));
    OutTable.insert_or_assign("detail_handbrake_torque_nm", static_cast<double>(Parameters.HandbrakeTorqueNm));
    OutTable.insert_or_assign("detail_brake_fade_start_c", static_cast<double>(Parameters.BrakeFadeStartC));
    OutTable.insert_or_assign("detail_max_brake_fade_pct", static_cast<double>(Parameters.MaxBrakeFadePct));
    OutTable.insert_or_assign("detail_maximum_catch_up_steps", static_cast<int64_t>(Parameters.MaximumCatchUpSteps));
    OutTable.insert_or_assign("detail_telemetry_sample_rate_hz", static_cast<int64_t>(Parameters.TelemetrySampleRateHz));
    OutTable.insert_or_assign("detail_selected_curve_profile", static_cast<int64_t>(Parameters.SelectedCurveProfile));
    OutTable.insert_or_assign("detail_selected_curve_point", static_cast<int64_t>(Parameters.SelectedCurvePoint));
    OutTable.insert_or_assign("detail_selected_corner_index", static_cast<int64_t>(Parameters.SelectedCornerIndex));

    WriteCurvePointsArray(OutTable, "oil_temperature_points", Parameters.OilTempCurve);
    WriteCurvePointsArray(OutTable, "tyre_surface_temperature_points", Parameters.TyreSurfaceTempCurve);

    toml::array CornersArray;
    for (const TyreCornerConfiguration& Corner : Parameters.Corners)
    {
        toml::table CornerTable;
        SerializeTyreCornerTable(CornerTable, Corner);
        CornersArray.push_back(std::move(CornerTable));
    }
    OutTable.insert_or_assign("corners", std::move(CornersArray));
}

template <>
void VehicleRecord::DeserializeConfiguration(const toml::table& InTable)
{
    if (const auto Val = InTable["identity"].value<std::string>()) Parameters.VehicleName = *Val;
    if (const auto Val = InTable["maker"].value<std::string>()) Parameters.MakerName = *Val;
    if (const auto Val = InTable["classification"].value<std::string>()) Parameters.ClassificationCode = *Val;
    Parameters.KerbMassKg              = ReadFloat(InTable, "kerb_mass_kg", Parameters.KerbMassKg);
    Parameters.WheelbaseMm             = ReadFloat(InTable, "wheelbase_mm", Parameters.WheelbaseMm);
    Parameters.TrackFrontMm            = ReadFloat(InTable, "track_front_mm", Parameters.TrackFrontMm);
    Parameters.TrackRearMm             = ReadFloat(InTable, "track_rear_mm", Parameters.TrackRearMm);
    Parameters.CentreOfGravityHeightMm = ReadFloat(InTable, "centre_of_gravity_height_mm", Parameters.CentreOfGravityHeightMm);
    Parameters.RideHeightMm            = ReadFloat(InTable, "ride_height_mm", Parameters.RideHeightMm);
    Parameters.FuelCapacityLitres      = ReadFloat(InTable, "fuel_capacity_l", Parameters.FuelCapacityLitres);
    Parameters.FuelLoadLitres          = ReadFloat(InTable, "fuel_load_l", Parameters.FuelLoadLitres);
    Parameters.StaticRearMassPercent   = ReadFloat(InTable, "rear_mass_share_percent", Parameters.StaticRearMassPercent);
    Parameters.DriveLayoutIndex        = std::clamp(ReadInt(InTable, "drive_layout_index", Parameters.DriveLayoutIndex), 0, 2);

    Parameters.DisplacementLitres          = ReadFloat(InTable, "displacement_l", Parameters.DisplacementLitres);
    Parameters.IdleSpeedRpm                = ReadFloat(InTable, "idle_speed_rpm", Parameters.IdleSpeedRpm);
    Parameters.RedlineRpm                  = ReadFloat(InTable, "redline_rpm", Parameters.RedlineRpm);
    Parameters.RatedTorqueNm               = ReadFloat(InTable, "rated_torque_nm", Parameters.RatedTorqueNm);
    Parameters.EngineInertiaKgm2           = ReadFloat(InTable, "engine_inertia_kgm2", Parameters.EngineInertiaKgm2);
    Parameters.InductionModeIndex          = std::clamp(ReadInt(InTable, "induction_mode_index", Parameters.InductionModeIndex), 0, 2);
    Parameters.RaceTuneEnabled             = ReadBool(InTable, "race_tune", Parameters.RaceTuneEnabled);
    Parameters.SuperchargerTypeIndex       = std::clamp(ReadInt(InTable, "supercharger_type_index", Parameters.SuperchargerTypeIndex), 0, 2);
    Parameters.SuperchargerDriveRatio      = ReadFloat(InTable, "supercharger_drive_ratio", Parameters.SuperchargerDriveRatio);
    Parameters.SuperchargerDisplacementCc  = ReadFloat(InTable, "supercharger_displacement_cc", Parameters.SuperchargerDisplacementCc);
    Parameters.SuperchargerMaxBoostBar     = ReadFloat(InTable, "supercharger_max_boost_bar", Parameters.SuperchargerMaxBoostBar);
    Parameters.SuperchargerPeakParasiticNm = ReadFloat(InTable, "supercharger_parasitic_nm", Parameters.SuperchargerPeakParasiticNm);
    Parameters.TurboBoostBar               = ReadFloat(InTable, "turbo_boost_bar", Parameters.TurboBoostBar);

    Parameters.ForwardGearCount      = std::clamp(ReadInt(InTable, "forward_gear_count", Parameters.ForwardGearCount), 4, 8);
    Parameters.FinalDriveRatio       = ReadFloat(InTable, "final_drive_ratio", Parameters.FinalDriveRatio);
    Parameters.DifferentialTypeIndex = std::clamp(ReadInt(InTable, "differential_type_index", Parameters.DifferentialTypeIndex), 0, 3);
    Parameters.LsdPreloadTorqueNm    = ReadFloat(InTable, "lsd_preload_nm", Parameters.LsdPreloadTorqueNm);
    Parameters.TorsenBiasRatio       = ReadFloat(InTable, "torsen_bias_ratio", Parameters.TorsenBiasRatio);

    Parameters.FrontSpringRateKnpm     = ReadFloat(InTable, "front_spring_kn_m", Parameters.FrontSpringRateKnpm);
    Parameters.RearSpringRateKnpm      = ReadFloat(InTable, "rear_spring_kn_m", Parameters.RearSpringRateKnpm);
    Parameters.AckermannPercent        = ReadFloat(InTable, "ackermann_percent", Parameters.AckermannPercent);
    Parameters.TractionControlEnabled  = ReadBool(InTable, "traction_control", Parameters.TractionControlEnabled);
    Parameters.StabilityControlEnabled = ReadBool(InTable, "stability_control", Parameters.StabilityControlEnabled);
    Parameters.AntiLockBrakesEnabled   = ReadBool(InTable, "anti_lock_brakes", Parameters.AntiLockBrakesEnabled);

    Parameters.SoftBodyFormulationIndex = std::clamp(ReadInt(InTable, "soft_body_formulation_index", Parameters.SoftBodyFormulationIndex), 0, 2);
    Parameters.SlipSolverModeIndex      = std::clamp(ReadInt(InTable, "slip_solver_mode_index", Parameters.SlipSolverModeIndex), 0, 2);
    Parameters.FemSubsteps              = std::clamp(ReadInt(InTable, "fem_substeps", Parameters.FemSubsteps), 4, 32);
    Parameters.FemYoungsModulusMpa      = ReadFloat(InTable, "fem_youngs_modulus_mpa", Parameters.FemYoungsModulusMpa);
    Parameters.FemPoissonRatio          = ReadFloat(InTable, "fem_poisson_ratio", Parameters.FemPoissonRatio);

    Parameters.OilViscosityGradeIndex = std::clamp(ReadInt(InTable, "oil_viscosity_grade_index", Parameters.OilViscosityGradeIndex), 0, 3);
    Parameters.CoolantTargetC         = ReadFloat(InTable, "coolant_target_c", Parameters.CoolantTargetC);
    Parameters.DragCoefficient        = ReadFloat(InTable, "drag_coefficient", Parameters.DragCoefficient);
    Parameters.DownforceAt200KmhN     = ReadFloat(InTable, "downforce_at_200_kmh_n", Parameters.DownforceAt200KmhN);
    Parameters.VenturiPeakGain        = ReadFloat(InTable, "venturi_peak_gain", Parameters.VenturiPeakGain);
    Parameters.LineLockFrontOnly      = ReadBool(InTable, "line_lock_front_only", Parameters.LineLockFrontOnly);

    Parameters.OuterTickHz              = std::clamp(ReadInt(InTable, "outer_tick_hz", Parameters.OuterTickHz), 30, 240);
    Parameters.SubstepsPerOuterTick     = std::clamp(ReadInt(InTable, "substeps_per_outer_tick", Parameters.SubstepsPerOuterTick), 1, 8);
    Parameters.StationarityHoldEnabled  = ReadBool(InTable, "stationarity_hold", Parameters.StationarityHoldEnabled);
    Parameters.ForceZoneCouplingEnabled = ReadBool(InTable, "force_zone_coupling", Parameters.ForceZoneCouplingEnabled);

    Parameters.CurveSmoothMonotone = ReadBool(InTable, "curve_smooth_monotone", Parameters.CurveSmoothMonotone);
    ReadCurvePointsArray(InTable, "engine_torque_points", Parameters.EngineTorqueCurve);
    ReadCurvePointsArray(InTable, "brake_torque_points", Parameters.BrakeTorqueCurve);
    ReadCurvePointsArray(InTable, "coolant_temperature_points", Parameters.CoolantTempCurve);

    Parameters.UnitDisplayImperial = ReadBool(InTable, "detail_unit_display_imperial", Parameters.UnitDisplayImperial);
    Parameters.InspectorModeIndex = ReadInt(InTable, "detail_inspector_mode_index", Parameters.InspectorModeIndex);
    Parameters.FuelReservePercent = ReadFloat(InTable, "detail_fuel_reserve_percent", Parameters.FuelReservePercent);
    Parameters.FuelBurnKgPerHour = ReadFloat(InTable, "detail_fuel_burn_kg_per_hour", Parameters.FuelBurnKgPerHour);
    Parameters.ThermostatOpeningC = ReadFloat(InTable, "detail_thermostat_opening_c", Parameters.ThermostatOpeningC);
    Parameters.SuperchargerBypassParasiticPct = ReadFloat(InTable, "detail_supercharger_bypass_parasitic_pct", Parameters.SuperchargerBypassParasiticPct);
    Parameters.SuperchargerBypassThrottlePct = ReadFloat(InTable, "detail_supercharger_bypass_throttle_pct", Parameters.SuperchargerBypassThrottlePct);
    Parameters.TurboSpoolUpRate = ReadFloat(InTable, "detail_turbo_spool_up_rate", Parameters.TurboSpoolUpRate);
    Parameters.TurboSpoolDownRate = ReadFloat(InTable, "detail_turbo_spool_down_rate", Parameters.TurboSpoolDownRate);
    Parameters.TorqueGainPerBar = ReadFloat(InTable, "detail_torque_gain_per_bar", Parameters.TorqueGainPerBar);
    Parameters.DrivelineEfficiencyPct = ReadFloat(InTable, "detail_driveline_efficiency_pct", Parameters.DrivelineEfficiencyPct);
    Parameters.LoadedWheelRadiusMm = ReadFloat(InTable, "detail_loaded_wheel_radius_mm", Parameters.LoadedWheelRadiusMm);
    Parameters.PeakTyreFriction = ReadFloat(InTable, "detail_peak_tyre_friction", Parameters.PeakTyreFriction);
    Parameters.LsdPowerLockPercent = ReadFloat(InTable, "detail_lsd_power_lock_percent", Parameters.LsdPowerLockPercent);
    Parameters.LsdCoastLockPercent = ReadFloat(InTable, "detail_lsd_coast_lock_percent", Parameters.LsdCoastLockPercent);
    Parameters.AwdFrontTorquePercent = ReadFloat(InTable, "detail_awd_front_torque_percent", Parameters.AwdFrontTorquePercent);
    Parameters.ShiftDurationMs = ReadFloat(InTable, "detail_shift_duration_ms", Parameters.ShiftDurationMs);
    Parameters.ClutchCapacityNm = ReadFloat(InTable, "detail_clutch_capacity_nm", Parameters.ClutchCapacityNm);
    Parameters.ProgressiveSpringFactor = ReadFloat(InTable, "detail_progressive_spring_factor", Parameters.ProgressiveSpringFactor);
    Parameters.FrontRestLengthMm = ReadFloat(InTable, "detail_front_rest_length_mm", Parameters.FrontRestLengthMm);
    Parameters.RearRestLengthMm = ReadFloat(InTable, "detail_rear_rest_length_mm", Parameters.RearRestLengthMm);
    Parameters.BumpStopStiffnessKnpm = ReadFloat(InTable, "detail_bump_stop_stiffness_knpm", Parameters.BumpStopStiffnessKnpm);
    Parameters.FrontBumpDampingNsm = ReadFloat(InTable, "detail_front_bump_damping_nsm", Parameters.FrontBumpDampingNsm);
    Parameters.FrontReboundDampingNsm = ReadFloat(InTable, "detail_front_rebound_damping_nsm", Parameters.FrontReboundDampingNsm);
    Parameters.RearBumpDampingNsm = ReadFloat(InTable, "detail_rear_bump_damping_nsm", Parameters.RearBumpDampingNsm);
    Parameters.RearReboundDampingNsm = ReadFloat(InTable, "detail_rear_rebound_damping_nsm", Parameters.RearReboundDampingNsm);
    Parameters.HighSpeedDampingRatio = ReadFloat(InTable, "detail_high_speed_damping_ratio", Parameters.HighSpeedDampingRatio);
    Parameters.DamperKneeVelocityMps = ReadFloat(InTable, "detail_damper_knee_velocity_mps", Parameters.DamperKneeVelocityMps);
    Parameters.FrontAntiRollKnpm = ReadFloat(InTable, "detail_front_anti_roll_knpm", Parameters.FrontAntiRollKnpm);
    Parameters.RearAntiRollKnpm = ReadFloat(InTable, "detail_rear_anti_roll_knpm", Parameters.RearAntiRollKnpm);
    Parameters.FrontStaticCamberDeg = ReadFloat(InTable, "detail_front_static_camber_deg", Parameters.FrontStaticCamberDeg);
    Parameters.RearStaticCamberDeg = ReadFloat(InTable, "detail_rear_static_camber_deg", Parameters.RearStaticCamberDeg);
    Parameters.MaxInnerSteerDeg = ReadFloat(InTable, "detail_max_inner_steer_deg", Parameters.MaxInnerSteerDeg);
    Parameters.SteeringRackRatio = ReadFloat(InTable, "detail_steering_rack_ratio", Parameters.SteeringRackRatio);
    Parameters.SpeedSensitiveSteerFloorPct = ReadFloat(InTable, "detail_speed_sensitive_steer_floor_pct", Parameters.SpeedSensitiveSteerFloorPct);
    Parameters.MechanicalTrailMm = ReadFloat(InTable, "detail_mechanical_trail_mm", Parameters.MechanicalTrailMm);
    Parameters.LaunchControlEnabled = ReadBool(InTable, "detail_launch_control_enabled", Parameters.LaunchControlEnabled);
    Parameters.DrsAidEnabled = ReadBool(InTable, "detail_drs_aid_enabled", Parameters.DrsAidEnabled);
    Parameters.RevLimiterEnabled = ReadBool(InTable, "detail_rev_limiter_enabled", Parameters.RevLimiterEnabled);
    Parameters.TelemetryEnabled = ReadBool(InTable, "detail_telemetry_enabled", Parameters.TelemetryEnabled);
    Parameters.EscYawGain = ReadFloat(InTable, "detail_esc_yaw_gain", Parameters.EscYawGain);
    Parameters.TcsSlipThresholdPct = ReadFloat(InTable, "detail_tcs_slip_threshold_pct", Parameters.TcsSlipThresholdPct);
    Parameters.AbsSlipThresholdPct = ReadFloat(InTable, "detail_abs_slip_threshold_pct", Parameters.AbsSlipThresholdPct);
    Parameters.CircumferentialNodeCount = ReadInt(InTable, "detail_circumferential_node_count", Parameters.CircumferentialNodeCount);
    Parameters.LateralRingCount = ReadInt(InTable, "detail_lateral_ring_count", Parameters.LateralRingCount);
    Parameters.VerticalStiffnessKnpm = ReadFloat(InTable, "detail_vertical_stiffness_knpm", Parameters.VerticalStiffnessKnpm);
    Parameters.PressureStiffnessGain = ReadFloat(InTable, "detail_pressure_stiffness_gain", Parameters.PressureStiffnessGain);
    Parameters.PneumaticVolumeWeightPct = ReadFloat(InTable, "detail_pneumatic_volume_weight_pct", Parameters.PneumaticVolumeWeightPct);
    Parameters.XpbdSubsteps = ReadInt(InTable, "detail_xpbd_substeps", Parameters.XpbdSubsteps);
    Parameters.XpbdIterations = ReadInt(InTable, "detail_xpbd_iterations", Parameters.XpbdIterations);
    Parameters.MaxContactBulgePct = ReadFloat(InTable, "detail_max_contact_bulge_pct", Parameters.MaxContactBulgePct);
    Parameters.FemVertexMassKg = ReadFloat(InTable, "detail_fem_vertex_mass_kg", Parameters.FemVertexMassKg);
    Parameters.FemDampingRateHz = ReadFloat(InTable, "detail_fem_damping_rate_hz", Parameters.FemDampingRateHz);
    Parameters.OilTargetC = ReadFloat(InTable, "detail_oil_target_c", Parameters.OilTargetC);
    Parameters.BrakeTargetC = ReadFloat(InTable, "detail_brake_target_c", Parameters.BrakeTargetC);
    Parameters.AmbientTemperatureC = ReadFloat(InTable, "detail_ambient_temperature_c", Parameters.AmbientTemperatureC);
    Parameters.CoolingCapacityKw = ReadFloat(InTable, "detail_cooling_capacity_kw", Parameters.CoolingCapacityKw);
    Parameters.CoolantHeatCapacityKjK = ReadFloat(InTable, "detail_coolant_heat_capacity_kj_k", Parameters.CoolantHeatCapacityKjK);
    Parameters.RadiatorAirflowGain = ReadFloat(InTable, "detail_radiator_airflow_gain", Parameters.RadiatorAirflowGain);
    Parameters.RadiatorFanThresholdC = ReadFloat(InTable, "detail_radiator_fan_threshold_c", Parameters.RadiatorFanThresholdC);
    Parameters.OverheatDerateStartC = ReadFloat(InTable, "detail_overheat_derate_start_c", Parameters.OverheatDerateStartC);
    Parameters.SpecificFuelBurnGkwh = ReadFloat(InTable, "detail_specific_fuel_burn_gkwh", Parameters.SpecificFuelBurnGkwh);
    Parameters.FrontalAreaM2 = ReadFloat(InTable, "detail_frontal_area_m2", Parameters.FrontalAreaM2);
    Parameters.TotalBrakeTorqueNm = ReadFloat(InTable, "detail_total_brake_torque_nm", Parameters.TotalBrakeTorqueNm);
    Parameters.BrakeBalanceFrontPct = ReadFloat(InTable, "detail_brake_balance_front_pct", Parameters.BrakeBalanceFrontPct);
    Parameters.RollingResistance = ReadFloat(InTable, "detail_rolling_resistance", Parameters.RollingResistance);
    Parameters.AeroBalanceFrontPct = ReadFloat(InTable, "detail_aero_balance_front_pct", Parameters.AeroBalanceFrontPct);
    Parameters.PitchAeroSensitivity = ReadFloat(InTable, "detail_pitch_aero_sensitivity", Parameters.PitchAeroSensitivity);
    Parameters.DrsDragReductionPct = ReadFloat(InTable, "detail_drs_drag_reduction_pct", Parameters.DrsDragReductionPct);
    Parameters.DrsDownforceLossPct = ReadFloat(InTable, "detail_drs_downforce_loss_pct", Parameters.DrsDownforceLossPct);
    Parameters.DrsActivationSpeedKmh = ReadFloat(InTable, "detail_drs_activation_speed_kmh", Parameters.DrsActivationSpeedKmh);
    Parameters.AirbrakeExtraCd = ReadFloat(InTable, "detail_airbrake_extra_cd", Parameters.AirbrakeExtraCd);
    Parameters.AirbrakeDownforceN = ReadFloat(InTable, "detail_airbrake_downforce_n", Parameters.AirbrakeDownforceN);
    Parameters.VenturiOptimalHeightMm = ReadFloat(InTable, "detail_venturi_optimal_height_mm", Parameters.VenturiOptimalHeightMm);
    Parameters.VenturiChokeHeightMm = ReadFloat(InTable, "detail_venturi_choke_height_mm", Parameters.VenturiChokeHeightMm);
    Parameters.HandbrakeTorqueNm = ReadFloat(InTable, "detail_handbrake_torque_nm", Parameters.HandbrakeTorqueNm);
    Parameters.BrakeFadeStartC = ReadFloat(InTable, "detail_brake_fade_start_c", Parameters.BrakeFadeStartC);
    Parameters.MaxBrakeFadePct = ReadFloat(InTable, "detail_max_brake_fade_pct", Parameters.MaxBrakeFadePct);
    Parameters.MaximumCatchUpSteps = ReadInt(InTable, "detail_maximum_catch_up_steps", Parameters.MaximumCatchUpSteps);
    Parameters.TelemetrySampleRateHz = ReadInt(InTable, "detail_telemetry_sample_rate_hz", Parameters.TelemetrySampleRateHz);
    Parameters.SelectedCurveProfile = ReadInt(InTable, "detail_selected_curve_profile", Parameters.SelectedCurveProfile);
    Parameters.SelectedCurvePoint = ReadInt(InTable, "detail_selected_curve_point", Parameters.SelectedCurvePoint);
    Parameters.SelectedCornerIndex = ReadInt(InTable, "detail_selected_corner_index", Parameters.SelectedCornerIndex);

    ReadCurvePointsArray(InTable, "oil_temperature_points", Parameters.OilTempCurve);
    ReadCurvePointsArray(InTable, "tyre_surface_temperature_points", Parameters.TyreSurfaceTempCurve);

    if (const toml::array* CornersArray = InTable["corners"].as_array())
    {
        for (size_t Idx = 0; Idx < std::min<size_t>(4u, CornersArray->size()); ++Idx)
        {
            if (const toml::table* CornerTable = (*CornersArray)[Idx].as_table())
            {
                DeserializeTyreCornerTable(*CornerTable, Parameters.Corners[Idx]);
            }
        }
    }
    Parameters.AdvanceRevision();
}

//------------------------------------------------------------------------------------------------------------------------
//                                      COMPOSABLE MULTI-CONFIGURATION SCENE RECORDS
//------------------------------------------------------------------------------------------------------------------------

SpatialNodeRecord::SpatialNodeRecord(std::string InDisplayName)
    : SceneRecordBase(RecordClassification::Geometry, "SpatialNode", std::move(InDisplayName))
{
    ConfigureEditorPresentation("Target", "Scene objects", "SpatialNode", "#a8bbeb");
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
    ConfigureEditorPresentation("Box", "Scene objects", "RigidBody", "#a8bbeb");
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
    ConfigureEditorPresentation("Thermometer", "Simulation", "ThermalBody", "#e8833a");
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
    ConfigureEditorPresentation("Droplets", "Simulation", "ChemicalVessel", "#e8833a");
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
                                                      const char*          GroupName,
                                                      const char*          AccentHex) {
        RegisterType({
            TypeId,
            CategoryLabel,
            Classification,
            [Classification, TypeId, IconName, GroupName, AccentHex](std::string DisplayName) -> std::unique_ptr<SceneRecordBase> {
                return std::make_unique<RecordType>(Classification, TypeId, std::move(DisplayName), IconName, GroupName, AccentHex);
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

    // 📝 Glyphs, groups and colours as the demo's entity list gives them (InspectorPages.js ConstructEntities,
    //    EnvironmentInspector.js EnvironmentEntries): Native* names are the environment artwork, the rest lucide strokes.
    // Environment Types
    RegisterSimple.operator()<SolarRecord>("Solar", "Environment · Solar", RecordClassification::Illumination, "NativeSun", "Environment", "#e8c27a");
    RegisterSimple.operator()<OpticalFlareRecord>("OpticalFlare", "Environment · Optical Flare", RecordClassification::Illumination, "NativeFlare", "Environment",
                                                  "#99aafa");
    RegisterSimple.operator()<AtmosphereRecord>("Atmosphere", "Environment · Atmosphere", RecordClassification::Environment, "NativeAtmosphere", "Environment",
                                                "#99aafa");
    RegisterSimple.operator()<CloudRecord>("Clouds", "Environment · Clouds", RecordClassification::Environment, "NativeClouds", "Environment", "#99aafa");
    RegisterType({"LocalCloud", "Environment · Local Cloud", RecordClassification::Environment,
        [](std::string Name) -> std::unique_ptr<SceneRecordBase>
        {
            auto Entry = std::make_unique<CloudRecord>(RecordClassification::Environment,"Clouds",std::move(Name),"NativeLocalCloud","Environment","#99aafa");
            Entry->AccessConfiguration().LocalVolume = true;
            return Entry;
        }});
    RegisterType({"LocalFog", "Environment · Local Fog", RecordClassification::Environment,
        [](std::string Name) -> std::unique_ptr<SceneRecordBase>
        {
            auto Entry = std::make_unique<FogRecord>(RecordClassification::Environment,"Fog",std::move(Name),"NativeLocalFog","Environment","#99aafa");
            Entry->AccessConfiguration().LocalVolume = true;
            return Entry;
        }});
    RegisterSimple.operator()<StarRecord>("Stars", "Environment · Stars", RecordClassification::Environment, "NativeStars", "Environment", "#99aafa");
    RegisterSimple.operator()<MoonRecord>("Moons", "Environment · Moons", RecordClassification::Environment, "NativeMoon", "Environment", "#99aafa");
    RegisterSimple.operator()<PlanetRecord>("Planet", "Environment · Planet", RecordClassification::Environment, "Orbit", "Environment", "#99aafa");
    RegisterSimple.operator()<FogRecord>("Fog", "Environment · Fog", RecordClassification::Environment, "NativeFog", "Environment", "#99aafa");
    RegisterType({"AerialFog", "Environment · Atmospheric Fog", RecordClassification::Environment,
        [](std::string Name) -> std::unique_ptr<SceneRecordBase>
        {
            auto Entry = std::make_unique<FogRecord>(RecordClassification::Environment, "AerialFog", std::move(Name),
                "NativeFog", "Environment", "#99aafa");
            Entry->AccessConfiguration().AerialOnly = true;
            return Entry;
        }});
    RegisterSimple.operator()<LightningRecord>("Lightning", "Environment · Lightning", RecordClassification::Illumination, "Sparkles", "Environment",
                                               "#a6c7ff");
    RegisterSimple.operator()<ForceFieldRecord>("ForceField", "Forcefield · Atmospheric Field", RecordClassification::Environment,
        "Wind", "Forcefield", "#a7f3d0");
    RegisterSimple.operator()<WindRecord>("Wind", "Environment · Wind Field", RecordClassification::Environment, "Wind", "Forcefield", "#a7f3d0");

    // 📝 The scene's objects, edited by the geometry inspector: the scene archive's own (a record claims their occupants) and the
    //    editor's (engine shapes and duplicates, laid out from the reserve); each record names its own group and glyph. Removing
    //    an archive object hides its occupants for as long as the level has no record claiming them.
    RegisterType({"Geometry", "Scene · Geometry", RecordClassification::Geometry,
        [](std::string Name) -> std::unique_ptr<SceneRecordBase>
        {
            return std::make_unique<GeometryRecord>(RecordClassification::Geometry, "Geometry", std::move(Name), "Box", "Geometry", "#a8bbeb");
        }});

    // Render settings (one record for the whole renderer) & Continuum Solver Types
    RegisterType({"RenderPipeline", "Render settings", RecordClassification::Illumination,
        [](std::string Name) -> std::unique_ptr<SceneRecordBase>
        {
            auto Entry = std::make_unique<RenderPipelineRecord>(RecordClassification::Illumination, "RenderPipeline", std::move(Name), "Monitor",
                                                                "Rendering", "#9fb4d6");
#if FRONTIER_EDITOR
            // 📝 The renderer's one component: the editor offers no Remove for it (the level would render with defaults).
            SceneDirectoryMetadata Meta = SceneDirectory::Instance().QueryMetadata(Entry->QueryToken());
            Meta.RemovableInEditor      = false;
            (void)SceneDirectory::Instance().ConfigureMetadata(Entry->QueryToken(), std::move(Meta));
#endif
            return Entry;
        }});
    RegisterSimple.operator()<FluidPlumeRecord>("FluidPlume", "Solver · Fluid Plume", RecordClassification::Physics, "Flame", "Solvers", "#e8833a");
    RegisterSimple.operator()<FractureRecord>("Fracture", "Solver · Voronoi Fracture", RecordClassification::Physics, "Hammer", "Solvers", "#8fb3a4");
    RegisterSimple.operator()<ParticleRecord>("Particles", "Solver · GPU Particles", RecordClassification::Physics, "Sparkles", "Solvers", "#b494f4");
    RegisterSimple.operator()<VehicleRecord>("Vehicle", "Vehicle · Chassis & Powertrain", RecordClassification::Physics, "VehicleSilhouette", "Vehicle", "#e8a84a");
    RegisterSimple.operator()<TyreRecord>("Tyre", "Vehicle · Corner Tyre", RecordClassification::Physics, "TyreTread", "Vehicle", "#8fc7b8");
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

RecordToken SceneWorld::ConstructTyreAssembly(std::string DisplayName, RecordToken Enclosure)
{
    const RecordToken TyreToken = ConstructRecord("Tyre", std::move(DisplayName), Enclosure);
    if (!TyreToken.IsValid()) return {};

    const RecordToken RimToken = ConstructRecord("Geometry", "Rim", TyreToken);
    auto* Rim = dynamic_cast<GeometryRecord*>(FindRecord(RimToken));
    if (Rim == nullptr)
    {
        DestroyRecord(TyreToken);
        return {};
    }
    GeometryConfiguration& RimConfiguration = Rim->AccessConfiguration();
    RimConfiguration.ClaimsArchive = false;
    RimConfiguration.ShapeName = "Torus";
    RimConfiguration.GroupName = "Vehicle";
    RimConfiguration.GlyphName = "RimSpokes";
    // 📝 The rim has no vehicle-relative placement yet; keep it hidden rather than placing it at the world origin.
    RimConfiguration.Visible = false;
    Rim->ConfigureEditorPresentation("RimSpokes", "Vehicle", "Geometry", "#a8bbeb");
    return TyreToken;
}

RecordToken SceneWorld::ConstructVehicleAssembly(std::string DisplayName)
{
    const RecordToken VehicleToken = ConstructRecord("Vehicle", std::move(DisplayName));
    if (!VehicleToken.IsValid()) return {};

    const char* Corners[] = {"FL", "FR", "RL", "RR"};
    std::vector<RecordToken> Created;
    for (size_t Index = 0; Index < 4; ++Index)
    {
        const RecordToken TyreToken = ConstructTyreAssembly(std::string("Tyre ") + Corners[Index], VehicleToken);
        if (!TyreToken.IsValid())
        {
            for (const RecordToken TyreTokenToRemove : Created)
            {
                for (const SceneRecordBase* Rim : FindAllByType("Geometry"))
                    if (Rim->QueryParentToken() == TyreTokenToRemove) { DestroyRecord(Rim->QueryToken()); break; }
                DestroyRecord(TyreTokenToRemove);
            }
            DestroyRecord(VehicleToken);
            return {};
        }
        Created.push_back(TyreToken);
        auto* Tyre = dynamic_cast<TyreRecord*>(FindRecord(TyreToken));
        auto* Vehicle = dynamic_cast<VehicleRecord*>(FindRecord(VehicleToken));
        if (Tyre != nullptr && Vehicle != nullptr)
        {
            Tyre->AccessConfiguration() = Vehicle->QueryConfiguration().Corners[Index];
            Tyre->AttachCorner(Vehicle->AccessConfiguration().Corners[Index]);
        }
    }
    return VehicleToken;
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
            if (auto* Tyre = dynamic_cast<TyreRecord*>(Candidate.get())) Tyre->DetachCorner();
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

std::vector<const SceneRecordBase*> SceneWorld::FindAllByType(std::string_view TypeIdentifier) const
{
    std::vector<const SceneRecordBase*> Found;
    for (const std::unique_ptr<SceneRecordBase>& Entry : OwnedRecords)
    {
        if (Entry && Entry->QueryTypeIdentifier() == TypeIdentifier) Found.push_back(Entry.get());
    }
    return Found;
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

    // 📝 Restore one fitted tyre per corner; unmatched or duplicate entries stay independent.
    for (const auto& Entry : OwnedRecords)
    {
        auto* Vehicle = dynamic_cast<VehicleRecord*>(Entry.get());
        if (Vehicle == nullptr) continue;
        std::array<bool, 4> Occupied{};
        for (const auto& Candidate : OwnedRecords)
        {
            auto* Tyre = dynamic_cast<TyreRecord*>(Candidate.get());
            if (Tyre == nullptr || Tyre->QueryParentToken() != Vehicle->QueryToken()) continue;
            for (size_t Index = 0; Index < Occupied.size(); ++Index)
            {
                auto& Corner = Vehicle->AccessConfiguration().Corners[Index];
                if (!Occupied[Index] && Corner.CornerCode == Tyre->QueryConfiguration().CornerCode)
                {
                    Tyre->AttachCorner(Corner);
                    Occupied[Index] = true;
                    break;
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
