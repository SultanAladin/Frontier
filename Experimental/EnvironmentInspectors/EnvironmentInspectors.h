/*==========================================================================================================================================
                                                   ENVIRONMENTINSPECTORS.H
==========================================================================================================================================*/
// 🧩 Host-owned authoring models. Rendering, persistence, and integration remain host responsibilities.
#pragma once
#include <cstdint>
#include <array>
#include <functional>
#include <vector>
struct ImDrawList;
namespace Frontier::Experimental
{
struct CloudSettings
{
    bool Enabled=true, Shadows=true, FollowWind=true, LayerStackOpen=false;
    float Coverage=.55f, Density=1.f, Scale=1.f, Anvil=.5f, Detail=.3f, Base=1500.f, Thickness=1200.f, Ceiling=14000.f;
    float Anisotropy=.35f, Absorption=.15f, Powder=.45f, LightSteps=2.f, Resolution=35.f, Steps=12.f, LocalSteps=16.f;
    float ShadowStrength=.65f, ShadowSoftness=35.f, ShadowDistance=6000.f, ShadowSamples=6.f;
    float GpuMilliseconds=-1.f; // Optional host metric; negative means unavailable.
    bool LightningEnabled=false, LightningAuto=false;
    float LightningDistance=1800.f, LightningIntervalMin=6.f, LightningIntervalMax=14.f;
    float LightningSpread=4200.f, LightningBrightness=1.f, LightningCloudGlow=1.f, LightningRadius=1800.f;
    int LightningMode=0, LightningCloudTarget=0;
    int Type=2, Tab=0; uint64_t Revision=0;
};
struct CloudActions
{
    std::function<void()> OpenWind, PlaceLocalCloud, TriggerLightning, ToggleThunder, OpenParticleEditor;
};
struct AtmosphereSettings
{
    bool Enabled=true, AirMass=true, MultipleScattering=true, LineAtCivilOnly=true, UseBakedAtmosphere=true, HorizonShapingOpen=false;
    float ProbeAltitude=8000.f, Rayleigh=1.f, Mie=1.f, Ozone=1.f, RayleighHeight=8000.f, MieHeight=1200.f;
    float MieG=.8f, SkyScale=1.f, HorizonGlow=1.f, WhiteLine=1.f, TierSamples=40.f;
    float SunElevation=38.f, SkyTint[3]={1.f,1.f,1.f}, GroundAlbedo[3]={.3f,.3f,.3f};
    int Tab=0, BakePreviewTab=0, SelectedScaleHeight=0; uint64_t Revision=0;
};
struct AtmosphereActions
{
    std::function<void()> Bake;
    std::function<void(ImDrawList*, float, float, float, float)> DrawSkyPreview;
    std::function<void(ImDrawList*, float, float, float, float, int)> DrawBakedPreview;
    uint64_t SkyRevision=0;
};
struct FlareSettings
{
    bool Enabled=true, Anamorphic=true, WarmStreak=false, Burst=true;
    float PreviewX=.28f, PreviewY=.43f, Intensity=.3f, Spread=1.f, Chromatic=.2f;
    float GhostCount=6.f, GhostBrightness=.3f, GhostSpacing=1.f;
    float HaloRadius=.22f, HaloWidth=.025f, HaloBrightness=.2f, Rotation=0.f, Rays=6.f, Streak=.3f, Aperture=6.f;
    int GhostShape=6, Preset=0, Tab=0; uint64_t Revision=0;
};
void ConstructCloudInspector(CloudSettings& Settings, const CloudActions& Actions = {});
std::array<double,3> AtmosphereTransmission(const AtmosphereSettings& Settings);
void ConstructAtmosphereInspector(AtmosphereSettings& Settings, const AtmosphereActions& Actions = {});
std::array<float,3> FlareRadiance(const FlareSettings& Settings, float U, float V, float Aspect);
std::vector<float> RenderFlareImage(const FlareSettings& Settings, int Width=640, int Height=280);
std::vector<uint8_t> EncodeFlarePfm(const std::vector<float>& Pixels, int Width, int Height);
using FlareExport = std::function<void(const std::vector<float>& Pixels, int Width, int Height)>;
void ConstructFlareInspector(FlareSettings& Settings, const FlareExport& Export = {});
}
