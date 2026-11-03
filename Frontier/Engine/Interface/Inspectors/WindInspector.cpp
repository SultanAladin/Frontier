/*==========================================================================================================================================
                                                             WINDINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 NativeWindPanel.js: FROM-bearing compass, exact gust envelope, three perturbation traces, altitude ladder and receivers.

#include "Engine/Interface/Inspectors/WindInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/PhysicalDynamics/AtmosphericFlow.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Inspectors/EnvironmentReadings.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

namespace
{

std::string Figure(const char *Format, double Quantity)
{
    char Text[80]{};
    std::snprintf(Text, sizeof(Text), Format, Quantity);
    return Text;
}

void BeginWindCard(ControlColumn &Page, const char *Heading, const char *Caption)
{
    CardLook Look;
    Look.Fill        = ResolveCssColour(0x252C23FF);
    Look.FillEnd     = ResolveCssColour(0x1D221EFF);
    Look.Border      = ResolveCssColour(0xB6CE9B21);
    Look.Radius      = 24;
    Look.PaddingSide = Look.PaddingTop = Look.PaddingBottom = 20;
    Look.MarginTop = Look.MarginBottom = 15;
    Look.GradientDegrees               = 140;
    Look.GradientStop                  = 1;
    Page.BeginCard(Look, nullptr, nullptr);
    SectionHeading::Construct(Page, Heading, Caption);
}

void Receiver(ControlColumn &Page, const char *Name, const char *Caption, bool *Follow, bool Enabled, RevisionCounter *Revision)
{
    const auto  &Faces = Page.QueryFaces();
    const float  Scale = Page.QueryScale();
    const ImVec2 Top   = Page.Space(112);
    const float  Right = Top.x + Page.QueryWidth();
    auto        *Ink   = Page.QueryDrawList();
    Ink->AddLine(Top, {Right, Top.y}, ResolveCssColour(0xB6CC991F), Scale);
    (void)GlyphRun::Construct(Page, Faces.Regular, 13 * Scale, {Top.x, GlyphRun::ResolveBaseline(Top.y + 19 * Scale, 13 * Scale, 1.4f)},
                              ResolveCssColour(0xCFDBC1FF), Name);
    (void)ParagraphBlock::Construct(Page, Faces.Regular, 10 * Scale, 1.6f, {Top.x, Top.y + 43 * Scale},
                                    std::min(175 * Scale, Page.QueryWidth() - 80 * Scale), ResolveCssColour(0x8F9F84FF), Caption);
    const bool Linked  = Follow && *Follow;
    bool       Pressed = false;
    if (!Follow) ImGui::BeginDisabled();
    TagPill::ConstructAt(Page, Name, {Right - 70 * Scale, Top.y + 19 * Scale}, Linked ? "Linked" : "Unlinked", true, &Pressed, 10, 10, 7, 0,
                         ResolveCssColour(Linked ? 0x35402EFF : 0x171E18FF), ResolveCssColour(Linked ? 0xD4DFB9FF : 0xA0AF94FF));
    if (!Follow) ImGui::EndDisabled();
    if (Pressed && Follow)
    {
        *Follow = !*Follow;
        if (Revision) Revision->AdvanceRevision();
    }
    (void)GlyphRun::Construct(Page, Faces.Regular, 9 * Scale, {Top.x, GlyphRun::ResolveBaseline(Top.y + 85 * Scale, 9 * Scale, 1.4f)},
                              ResolveCssColour(0xA5B195FF),
                              !Follow   ? "Receiver unavailable"
                              : Enabled ? "Receiver enabled"
                                        : "Receiver off");
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    BASE FLOW
//------------------------------------------------------------------------------------------------------------------------

bool WindInspector::ConstructFlow(ControlColumn &Page)
{
    BeginWindCard(Page, "Direction of travel", "01 / BASE FLOW");
    const ImVec2 Summary = Page.QueryCursor();
    ReadoutBlock::Construct(Page, "", Figure("%.1f", Parameters.SpeedMetresPerSecond).c_str(), "m/s", 53, 12, -2, 1);
    const auto  Bearing = Figure("FROM %.0f°", EnvironmentReadings::WrapDegrees(Parameters.DirectionDegrees + 180));
    const float Advance = GlyphRun::Measure(Page, Page.QueryFaces().Regular, 10 * Page.QueryScale(), Bearing.c_str());
    (void)GlyphRun::Construct(Page, Page.QueryFaces().Regular, 10 * Page.QueryScale(),
                              {Summary.x + Page.QueryWidth() - Advance, Summary.y + 36 * Page.QueryScale()}, ResolveCssColour(0x9DB08EFF),
                              Bearing.c_str());
    DiagramDrag Drag;
    auto        Pen     = DiagramCanvas::Begin(Page, "WindCompass", 640, 480, 15, 20, &Drag,
                                               "Wind flow compass · drag source bearing and speed; arrows edit bearing or speed");
    bool        Changed = Drag.Engaged || Drag.Key != ImGuiKey_None;
    if (Drag.Engaged)
    {
        const float Along = Drag.Point.x - 320, Across = 235 - Drag.Point.y;
        Parameters.DirectionDegrees =
            EnvironmentReadings::WrapDegrees(static_cast<float>(std::atan2(Along, Across) * 180 / EnvironmentReadings::Pi) + 180);
        Parameters.SpeedMetresPerSecond = std::clamp(std::hypot(Along, Across) / 155 * 40, 0.0f, 40.0f);
    }
    if (Drag.Key == ImGuiKey_Home)
    {
        Parameters.DirectionDegrees     = 180;
        Parameters.SpeedMetresPerSecond = 7;
    }
    if (Drag.Key == ImGuiKey_LeftArrow || Drag.Key == ImGuiKey_RightArrow)
        Parameters.DirectionDegrees =
            EnvironmentReadings::WrapDegrees(Parameters.DirectionDegrees + (Drag.Key == ImGuiKey_RightArrow ? 1 : -1) * Drag.Step);
    if (Drag.Key == ImGuiKey_UpArrow || Drag.Key == ImGuiKey_DownArrow)
        Parameters.SpeedMetresPerSecond =
            std::clamp(Parameters.SpeedMetresPerSecond + (Drag.Key == ImGuiKey_UpArrow ? 0.1f : -0.1f) * Drag.Step, 0.0f, 40.0f);
    const float  θ = static_cast<float>(Parameters.DirectionDegrees * EnvironmentReadings::Pi / 180);
    const ImVec2 Forward(std::sin(θ), -std::cos(θ)), Lateral(-Forward.y, Forward.x);
    auto         Point = [&](float Along, float Across)
    { return ImVec2(320 + Forward.x * Along + Lateral.x * Across, 235 + Forward.y * Along + Lateral.y * Across); };
    Pen.Circle({320, 235}, 155, 0, ResolveCssColour(0xC1D6B82D));
    Pen.Circle({320, 235}, 98, 0, ResolveCssColour(0xC1D6B814));
    for (int Tick = 0; Tick < 24; ++Tick)
    {
        const float Angle = static_cast<float>(Tick / 24.0 * EnvironmentReadings::Pi * 2);
        Pen.Line({320 + std::sin(Angle) * 150, 235 - std::cos(Angle) * 150}, {320 + std::sin(Angle) * 155, 235 - std::cos(Angle) * 155},
                 ResolveCssColour(0xBFD1B258));
    }
    for (int Lane = -2; Lane <= 2; ++Lane)
    {
        const float         Across = Lane * 39.0f, Length = std::sqrt(std::max(0.0f, 135 * 135 - Across * Across));
        std::vector<ImVec2> Points;
        for (int Sample = 0; Sample <= 36; ++Sample)
            Points.push_back(Point(-Length + 2 * Length * Sample / 36,
                                   Across + std::sin(Sample / 36.0f * static_cast<float>(EnvironmentReadings::Pi)) * Lane * 3));
        Pen.Path(Points, ResolveCssColour(0xB9C9A12A), 1.5f);
        if (Parameters.SpeedMetresPerSecond > 0)
        {
            const auto End = Point(Length * 0.48f, Across);
            Pen.Line(Point(Length * 0.48f - 12, Across), End, ResolveCssColour(0xC7D8ACFF), 2);
            Pen.Path({Point(Length * 0.48f - 9, Across - 5), End, Point(Length * 0.48f - 9, Across + 5)}, ResolveCssColour(0xC7D8ACFF), 2);
        }
    }
    const auto From = Point(-155 * Parameters.SpeedMetresPerSecond / 40, 0);
    Pen.Line({320, 235}, From, ResolveCssColour(0xDFE4BA66), 1, 4, 6);
    Pen.Circle(From, 9, ResolveCssColour(0xE1E5BCFF));
    Pen.Circle({320, 235}, 4, ResolveCssColour(0x94AB8AFF));
    Pen.Text({320, 54}, "N / +Y", ResolveCssColour(0xA7B69BFF), 19);
    Pen.Text({516, 240}, "E", ResolveCssColour(0xA7B69BFF), 19);
    Pen.Text({320, 433}, "S", ResolveCssColour(0xA7B69BFF), 19);
    Pen.Text({126, 240}, "W", ResolveCssColour(0xA7B69BFF), 19);
    Pen.Text({320, 475}, "DOT = WIND FROM / ARROWS = TRAVEL TO", ResolveCssColour(0x91A38CFF), 17);
    Changed |= SliderRow::Construct(Page, "CloudWindSpeed", "Base speed", Parameters.SpeedMetresPerSecond, 0, 40, 0.1f, "m/s");
    float FromBearing = EnvironmentReadings::WrapDegrees(Parameters.DirectionDegrees + 180);
    if (SliderRow::Construct(Page, "CloudWindBearing", "Bearing from", FromBearing, 0, 360, 1, "°"))
    {
        Parameters.DirectionDegrees = EnvironmentReadings::WrapDegrees(FromBearing + 180);
        Changed                     = true;
    }
    CaptionText::Construct(Page,
                           "Meteorological bearing is where wind comes FROM; the arrows show where it travels TO. Drag the source dot "
                           "radially for base speed. Base-only reference: gusts, altitude and turbulence are excluded.",
                           18, 0);
    Page.EndCard();
    const auto Readout = InspectorRuntime::Instance().QueryEnvironmentAccess().Rendering;
    if (Readout && Readout->Ready)
        CaptionText::Construct(Page,
                               ("Actual field speed at camera: " +
                                Figure("%.2f", std::hypot(Readout->WindAtCamera[0], Readout->WindAtCamera[1], Readout->WindAtCamera[2])) +
                                " m/s")
                                   .c_str(),
                               0, 16);
    else
        CaptionText::Construct(Page, "No live viewport sample. Diagrams show the configured field, not measured history.", 0, 16);
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VARIABILITY
//------------------------------------------------------------------------------------------------------------------------

bool WindInspector::ConstructVariation(ControlColumn &Page)
{
    BeginWindCard(Page, "The gust envelope", "02 / SPEED MODULATION");
    auto        Pen      = DiagramCanvas::Begin(Page, "GustEnvelope", 640, 290, 15, 20);
    const float Peak     = std::max(1.0f, Parameters.SpeedMetresPerSecond * (1 + Parameters.GustStrength));
    const auto  Vertical = [&](double Speed) { return static_cast<float>(230 - Speed / Peak * 180); };
    Pen.Rect({45, Vertical(Parameters.SpeedMetresPerSecond * (1 + Parameters.GustStrength))},
             {550, std::max(1.0f, Vertical(Parameters.SpeedMetresPerSecond * (1 - Parameters.GustStrength)) -
                                      Vertical(Parameters.SpeedMetresPerSecond * (1 + Parameters.GustStrength)))},
             ResolveCssColour(0xB9D1A611), 8);
    Pen.Line({45, Vertical(Parameters.SpeedMetresPerSecond)}, {595, Vertical(Parameters.SpeedMetresPerSecond)},
             ResolveCssColour(0xC3D1AC44), 1, 3, 5);
    std::vector<ImVec2>     Points;
    ForceFieldSpace         Field;
    ForceFieldConfiguration Controls;
    auto                    Reference = Parameters;
    Reference.Enabled                 = true;
    Reference.CurlNoiseTurbulence     = 0;
    for (int Sample = 0; Sample <= 160; ++Sample)
    {
        ConfigureAtmosphericFlow(Field, Controls, Reference, Sample / 160.0 * 40);
        const auto Velocity = EvaluateWorldFlow(Field, {0, 0, 0}, false);
        Points.push_back({45 + Sample / 160.0f * 550, Vertical(std::hypot(Velocity[0], Velocity[1], Velocity[2]))});
    }
    Pen.Path(Points, ResolveCssColour(0xCEDAB4FF), 2);
    Pen.Text({45, 27}, "SPEED / m/s", ResolveCssColour(0x93A38AFF), 18, -1);
    Pen.Text({595, 27}, Figure("%.1f MAX BOUND", Peak).c_str(), ResolveCssColour(0xC4CEB0FF), 18, 1);
    Pen.Text({45, 273}, "0 s", ResolveCssColour(0x93A38AFF), 18, -1);
    Pen.Text({595, 273}, "40 s", ResolveCssColour(0x93A38AFF), 18, 1);
    bool Changed = SliderRow::Construct(Page, "WindGust", "Gust strength", Parameters.GustStrength, 0, 1, 0.01f, "");
    Changed |= SliderRow::Construct(Page, "GustFrequency", "Gust frequency", Parameters.GustFrequencyHz, .01f, 2, .005f, "Hz");
    CaptionText::Construct(
        Page,
        "Native force-field speed modulation under fixed settings, starting at phase zero. Shading is the guaranteed ±gust "
        "bound, not a measured history or a forecast of the running clock.",
        18, 0);
    Page.EndCard();
    BeginWindCard(Page, "Cross-flow perturbation", "03 / THREE VELOCITY COMPONENTS");
    Pen                                             = DiagramCanvas::Begin(Page, "TurbulenceComponents", 640, 240, 15, 20);
    constexpr uint32_t                    Colours[] = {0xD2D7AAFF, 0xABC9BBFF, 0xA7BCCBFF};
    constexpr const char                 *Axes[]    = {"X", "Y", "Z"};
    std::array<std::array<double, 3>, 81> Swirls;
    double                                DisplayScale = .01;
    Reference                                          = Parameters;
    Reference.Enabled                                  = true;
    Reference.GustStrength                             = 0;
    for (size_t Sample = 0; Sample < Swirls.size(); ++Sample)
    {
        ConfigureAtmosphericFlow(Field, Controls, Reference, Sample / 80.0 * 20);
        const auto Swirl = Field.SampleSwirl({0, 10, 0});
        Swirls[Sample]   = {Swirl.X, -Swirl.Z, Swirl.Y};
        for (double Component : Swirls[Sample])
            DisplayScale = std::max(DisplayScale, std::abs(Component));
    }
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const float Height = 45.0f + Axis * 66;
        Pen.Line({95, Height}, {590, Height}, ResolveCssColour(0xB9CBA220));
        Points.clear();
        for (int Sample = 0; Sample <= 80; ++Sample)
        {
            const double Signal = Swirls[Sample][Axis] / DisplayScale;
            Points.push_back({95 + Sample / 80.0f * 495, Height - static_cast<float>(Signal * 24)});
        }
        Pen.Path(Points, ResolveCssColour(Colours[Axis]), 2);
        Pen.Text({45, Height + 5}, Axes[Axis], ResolveCssColour(0xA9BAA3FF), 20);
        Pen.Text({590, Height + 29}, Figure("%.2f m/s display scale", DisplayScale).c_str(), ResolveCssColour(0x91A18CFF), 16, 1);
    }
    Changed |= SliderRow::Construct(Page, "WindTurbulence", "Turbulence", Parameters.CurlNoiseTurbulence, 0, 1, 0.01f, "");
    Changed |= SliderRow::Construct(Page, "TurbulenceScale", "Turbulence scale", Parameters.TurbulenceScaleMetres, 1, 1000, 1, "m");
    CaptionText::Construct(Page,
                           "Production curl-field samples at 10 m, with fixed parameters over 20 seconds. These are deterministic solver "
                           "evaluations, not a recording of the viewport.",
                           18, 0);
    Page.EndCard();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ALTITUDE AND RECEIVERS
//------------------------------------------------------------------------------------------------------------------------

bool WindInspector::ConstructAltitude(ControlColumn &Page)
{
    BeginWindCard(Page, "Wind through altitude", "04 / STEADY VECTOR LADDER");
    auto        Pen   = DiagramCanvas::Begin(Page, "WindAltitude", 640, 390, 15, 20);
    const float Scale = std::max(1.0f, Parameters.SpeedMetresPerSecond * (1 + Parameters.ShearPerKilometre * 3));
    Pen.Line({270, 40}, {270, 330}, ResolveCssColour(0xA8BD9B33), 1, 3, 5);
    for (int Tier = 0; Tier < 6; ++Tier)
    {
        const float Height = Tier * 600.0f, Vertical = 330 - Tier * 55.0f;
        const double θ = (Parameters.DirectionDegrees + Parameters.VeerDegreesPerKilometre * Height / 1000) * EnvironmentReadings::Pi / 180;
        const float Speed  = Parameters.SpeedMetresPerSecond * (1 + Parameters.ShearPerKilometre * Height / 1000),
                    Length = Speed / Scale * 120;
        const ImVec2 End(270 + static_cast<float>(std::sin(θ)) * Length, Vertical - static_cast<float>(std::cos(θ)) * Length * 0.28f);
        Pen.Text({90, Vertical + 6}, Tier ? Figure("%.1f km", Height / 1000).c_str() : "0 m", ResolveCssColour(0x9AAD94FF), 19);
        Pen.Line({270, Vertical}, End, ResolveCssColour(0xC7D8AEFF), 2.5f);
        if (Length > 0) Pen.Circle(End, 4, ResolveCssColour(0xDCE3BEFF));
        Pen.Text({590, Vertical + 6}, Figure("%.1f m/s", Speed).c_str(), ResolveCssColour(0xC8D3B6FF), 20, 1);
    }
    Pen.Text({45, 380}, "PLAN DIRECTIONS / +Y NORTH · 0–3 km", ResolveCssColour(0x91A18CFF), 18, -1);
    bool Changed = SliderRow::Construct(Page, "WindShear", "Speed gain per km", Parameters.ShearPerKilometre, 0, 2, 0.01f, "×/km");
    Changed |= SliderRow::Construct(Page, "WindVeer", "Direction veer per km", Parameters.VeerDegreesPerKilometre, -60, 60, 1, "°/km");
    CaptionText::Construct(
        Page,
        "Steady speed = base × (1 + shear × altitude in km). Travel bearing turns by veer × altitude in km. Negative altitude is clamped "
        "to sea level. Gusts and turbulence are excluded here; vectors are plan-view glyphs, not vertically tilted air.",
        18, 0);
    Page.EndCard();
    return Changed;
}

void WindInspector::ConstructReceivers(ControlColumn &Page)
{
    BeginWindCard(Page, "Where the field goes", "05 / SHARED ADVECTION ROUTING");
    const auto &Access = InspectorRuntime::Instance().QueryEnvironmentAccess();
    Receiver(Page, "Clouds", "Density travel at cloud-base altitude", Access.Clouds ? &Access.Clouds->FollowWind : nullptr,
             Access.Clouds && Access.Clouds->Enabled, Access.Clouds);
    Receiver(Page, "Local cloud", "Placed bounds stay fixed; density travels", Access.LocalCloud ? &Access.LocalCloud->FollowWind : nullptr,
             Access.LocalCloud && Access.LocalCloud->Enabled, Access.LocalCloud);
    Receiver(Page, "Local fog", "Unlinked uses its own authored drift", Access.LocalFog ? &Access.LocalFog->FollowWind : nullptr,
             Access.LocalFog && Access.LocalFog->Enabled, Access.LocalFog);
    CaptionText::Construct(Page,
                           "Linking a receiver does not enable it. Static height fog has no advected density. Tyre smoke/fire retains its "
                           "separate gas-wind controls.",
                           18, 0);
    CaptionText::Construct(Page,
                           "Cloud and fog links drive native density advection. Vegetation and precipitation consumers are not registered "
                           "here; their response is not claimed.",
                           18, 0);
    Page.EndCard();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE DISPATCH
//------------------------------------------------------------------------------------------------------------------------

void WindInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &OutSwitches)
{
    OutSwitches.push_back({"Advect Clouds", "Global layer", &Parameters.AdvectClouds});
}

void WindInspector::ConstructInspector()
{
    const auto    Theme = ControlTheme::ResolveWind();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    bool Changed = PageHeading::Construct(Page, "WindPower", "FORCEFIELD / SHARED WEATHER", "Wind", Parameters.Enabled ? "● On" : "○ Off",
                                          "A direction, a pulse, a changing column of air.", "Toggle shared wind");
    if (Changed) Parameters.Enabled = !Parameters.Enabled;
    constexpr const char *Tabs[] = {"Flow", "Variability", "Altitude", "Receivers"};
    TabStrip::Construct(Page, "WindTabs", Tabs, 4, ActivePane);
    switch (ActivePane)
    {
    case 0:
        Changed |= ConstructFlow(Page);
        break;
    case 1:
        Changed |= ConstructVariation(Page);
        break;
    case 2:
        Changed |= ConstructAltitude(Page);
        break;
    case 3:
        ConstructReceivers(Page);
        break;
    }
    if (NextButton::Construct(Page, "OpenForceField", "Open Force Field / world vectors ↗",
                              InspectorRuntime::Instance().QueryEnvironmentAccess().Forces != nullptr, "Add a Force Field record first."))
        InspectorRuntime::Instance().RequestSelection("ForceField");
    CaptionText::Construct(Page, "Wind is one contributor to the atmospheric Force Field. World vectors and media use the same solver. A "
                                 "disabled parent field suspends its contribution without changing these authored coefficients.");
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void WindInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry> &) {}
void WindInspector::ConstructInspector() {}
} // namespace Frontier

#endif
