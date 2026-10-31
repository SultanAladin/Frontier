/*==========================================================================================================================================
                                                      SOLARINSPECTOR.H
==========================================================================================================================================*/
// 🧩 Independent authoring state for an experimental Sun inspector; host owns and applies it.
#pragma once
#include <cstdint>
#include <functional>
namespace Frontier::Experimental
{
struct SolarSettings
{
    bool Enabled = true, DiscEnabled = true, AutoPosition = false, DayCycle = false, UseRgb = false;
    float ElevationDegrees = 38.f, AzimuthDegrees = 145.f, Multiplier = 12.f, Temperature = 5778.f;
    float RadiusDegrees = .27f, Glow = 1.f, Time = 12.f, DayDuration = 6.f;
    float Latitude = -26.18f, Longitude = 28.32f, TimeZone = 2.f;
    float Tint[3] = {1.f, 1.f, 1.f};
    int Year = 2026, Month = 10, Day = 20, Tab = 0, Preset = -1;
    uint64_t Revision = 0;
};
struct SunPosition { double Azimuth, Elevation; };
SunPosition SolveSun(const SolarSettings& Settings, double Hour);
void AdvanceSun(SolarSettings& Settings, double Seconds);
void ConstructSolarInspector(SolarSettings& Settings, const std::function<void()>& OpenFlare = {});
}
