/*==========================================================================================================================================
                                                         RENDERSETTINGSSECTIONS.H
==========================================================================================================================================*/
// 🧩 The Render settings page's sections, one class per file: resolution and sampling, global illumination, reflections, sun and
//    shadows, denoising, exposure, geometry, debug views and refresh timing; plus the rows they share.

#pragma once

#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsReadings.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHARED ROWS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The page's rows over the shared controls: an Unavailable reason fades the row and becomes its tooltip.
namespace RenderSettingsRows
{
void BeginSection(ControlColumn& Page, const char* Title, const char* Glyph, const char* Status, const char* Caption);
void EndSection(ControlColumn& Page);
void SubHeading(ControlColumn& Page, const char* Title);
bool Switch(ControlColumn& Page, const char* Id, const char* Label, bool& Setting, const char* Unavailable = nullptr, const char* Tooltip = nullptr);
bool Count(ControlColumn& Page, const char* Id, const char* Label, int& Setting, int Minimum, int Maximum, const char* Unit,
           const char* Unavailable = nullptr, const char* Tooltip = nullptr);
bool Amount(ControlColumn& Page, const char* Id, const char* Label, float& Setting, float Minimum, float Maximum, float Step, const char* Unit,
            const char* Unavailable = nullptr, const char* Tooltip = nullptr);
} // namespace RenderSettingsRows

//------------------------------------------------------------------------------------------------------------------------
//                                                    SECTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each Construct draws its section and returns true when a setting changed.
class SamplingSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class IlluminationSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class ReflectionSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class ShadowSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class DenoiseSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class ExposureSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class GeometrySection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class ViewSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

class TimingSection
{
  public:
    static bool Construct(ControlColumn& Page, RenderPipelineConfiguration& Parameters, const RenderSettingsReading& Reading);
};

} // namespace Frontier
