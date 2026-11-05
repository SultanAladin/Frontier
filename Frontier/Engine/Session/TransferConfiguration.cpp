/*==========================================================================================================================================
                                                         TRANSFERCONFIGURATION.CPP
==========================================================================================================================================*/
// 🧩 TOML serialization and deserialization for TransferConfiguration.

#include "Frontier/Engine/Session/TransferConfiguration.h"

#include <sstream>
#include <toml++/toml.hpp>

namespace Frontier
{

std::string TransferConfiguration::SerializeToToml() const
{
    toml::table Root;
    Root.insert("source_level_path",  SourceLevelPath);
    Root.insert("target_level_path",  TargetLevelPath);
    Root.insert("target_policy_name", TargetPolicyName);
    Root.insert("transition_epoch",   static_cast<int64_t>(TransitionEpoch));

    toml::array PlayersArray;
    for (const PlayerProfile& Profile : PreservedProfiles)
    {
        toml::table Entry;
        Entry.insert("token",                   static_cast<int64_t>(Profile.Token.Value));
        Entry.insert("display_name",            Profile.DisplayName);
        Entry.insert("network_endpoint",        Profile.NetworkEndpoint);
        Entry.insert("local_host",              Profile.IsLocalHost);
        Entry.insert("synthetic_ai",            Profile.SyntheticAi);
        Entry.insert("ready",                   Profile.Ready);
        Entry.insert("stored_unit_token",       static_cast<int64_t>(Profile.StoredUnitToken.Value));
        Entry.insert("score",                   static_cast<int64_t>(Profile.Score));

        Entry.insert("paint_name",              Profile.VehicleSpec.PaintName);
        Entry.insert("paint_r",                 static_cast<double>(Profile.VehicleSpec.PaintAlbedo[0]));
        Entry.insert("paint_g",                 static_cast<double>(Profile.VehicleSpec.PaintAlbedo[1]));
        Entry.insert("paint_b",                 static_cast<double>(Profile.VehicleSpec.PaintAlbedo[2]));
        Entry.insert("paint_roughness",         static_cast<double>(Profile.VehicleSpec.PaintRoughness));
        Entry.insert("paint_metallic",          static_cast<double>(Profile.VehicleSpec.PaintMetallic));
        Entry.insert("rim_finish_name",         Profile.VehicleSpec.RimFinishName);
        Entry.insert("caliper_name",            Profile.VehicleSpec.CaliperName);
        Entry.insert("headlight_optics_name",   Profile.VehicleSpec.HeadlightOpticsName);
        Entry.insert("backdrop",                static_cast<int64_t>(Profile.VehicleSpec.Backdrop));
        Entry.insert("ride_height_offset_m",    static_cast<double>(Profile.VehicleSpec.RideHeightOffsetMetres));
        Entry.insert("camber_front_deg",        static_cast<double>(Profile.VehicleSpec.CamberFrontDegrees));
        Entry.insert("camber_rear_deg",         static_cast<double>(Profile.VehicleSpec.CamberRearDegrees));
        Entry.insert("front_pressure_kpa",      static_cast<double>(Profile.VehicleSpec.FrontPressureKpa));
        Entry.insert("rear_pressure_kpa",       static_cast<double>(Profile.VehicleSpec.RearPressureKpa));

        PlayersArray.push_back(std::move(Entry));
    }
    Root.insert("players", std::move(PlayersArray));

    std::ostringstream Stream;
    Stream << Root;
    return Stream.str();
}

bool TransferConfiguration::DeserializeFromToml(std::string_view TomlText)
{
    try
    {
        const toml::table Root = toml::parse(TomlText);
        SourceLevelPath  = Root["source_level_path"].value_or(std::string{});
        TargetLevelPath  = Root["target_level_path"].value_or(std::string{});
        TargetPolicyName = Root["target_policy_name"].value_or(std::string{});
        TransitionEpoch  = static_cast<uint64_t>(Root["transition_epoch"].value_or<int64_t>(0));

        PreservedProfiles.clear();
        if (const auto* PlayersArray = Root["players"].as_array())
        {
            for (const toml::node& Node : *PlayersArray)
            {
                const auto* Table = Node.as_table();
                if (Table == nullptr)
                {
                    continue;
                }
                PlayerProfile Profile{};
                Profile.Token.Value                 = static_cast<uint64_t>((*Table)["token"].value_or<int64_t>(0));
                Profile.DisplayName                 = (*Table)["display_name"].value_or(std::string{"Driver"});
                Profile.NetworkEndpoint             = (*Table)["network_endpoint"].value_or(std::string{"local"});
                Profile.IsLocalHost                 = (*Table)["local_host"].value_or(false);
                Profile.SyntheticAi                 = (*Table)["synthetic_ai"].value_or(false);
                Profile.Ready                       = (*Table)["ready"].value_or(false);
                Profile.StoredUnitToken.Value       = static_cast<uint64_t>((*Table)["stored_unit_token"].value_or<int64_t>(0));
                Profile.Score                       = static_cast<int32_t>((*Table)["score"].value_or<int64_t>(0));

                Profile.VehicleSpec.PaintName              = (*Table)["paint_name"].value_or(std::string{"Obsidian Black"});
                Profile.VehicleSpec.PaintAlbedo[0]         = static_cast<float>((*Table)["paint_r"].value_or(0.03));
                Profile.VehicleSpec.PaintAlbedo[1]         = static_cast<float>((*Table)["paint_g"].value_or(0.032));
                Profile.VehicleSpec.PaintAlbedo[2]         = static_cast<float>((*Table)["paint_b"].value_or(0.036));
                Profile.VehicleSpec.PaintRoughness         = static_cast<float>((*Table)["paint_roughness"].value_or(0.08));
                Profile.VehicleSpec.PaintMetallic          = static_cast<float>((*Table)["paint_metallic"].value_or(0.88));
                Profile.VehicleSpec.RimFinishName          = (*Table)["rim_finish_name"].value_or(std::string{"Satin Gunmetal"});
                Profile.VehicleSpec.CaliperName            = (*Table)["caliper_name"].value_or(std::string{"Acid Green"});
                Profile.VehicleSpec.HeadlightOpticsName    = (*Table)["headlight_optics_name"].value_or(std::string{"Xenon Ice White"});
                Profile.VehicleSpec.Backdrop               = static_cast<StudioBackdropStyle>((*Table)["backdrop"].value_or<int64_t>(0));
                Profile.VehicleSpec.RideHeightOffsetMetres = static_cast<float>((*Table)["ride_height_offset_m"].value_or(0.0));
                Profile.VehicleSpec.CamberFrontDegrees     = static_cast<float>((*Table)["camber_front_deg"].value_or(-1.8));
                Profile.VehicleSpec.CamberRearDegrees      = static_cast<float>((*Table)["camber_rear_deg"].value_or(-2.2));
                Profile.VehicleSpec.FrontPressureKpa       = static_cast<float>((*Table)["front_pressure_kpa"].value_or(220.0));
                Profile.VehicleSpec.RearPressureKpa        = static_cast<float>((*Table)["rear_pressure_kpa"].value_or(240.0));

                PreservedProfiles.push_back(std::move(Profile));
            }
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace Frontier
