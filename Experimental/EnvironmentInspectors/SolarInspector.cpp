/*==========================================================================================================================================
                                               SOLARINSPECTORPRESENTATION.CPP
==========================================================================================================================================*/
// 🧩 Reference-led Sun overview, using the editor's shared authoring slider.

#include "Experimental/EnvironmentInspectors/SolarInspector.h"
#include "Experimental/EnvironmentInspectors/EnvironmentControls.h"
#include "Experimental/EnvironmentInspectors/ObservatoryTypography.h"
#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/InspectorControls.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace Frontier::Experimental
{
namespace
{
constexpr float Pi = 3.14159265358979323846f;
constexpr ImU32 Ink = IM_COL32(232, 231, 223, 255);
constexpr ImU32 Muted = IM_COL32(161, 151, 134, 255);
constexpr ImU32 Fine = IM_COL32(160, 150, 125, 255);
constexpr ImU32 Accent = IM_COL32(232, 199, 139, 255);

float TextWidth(ImFont* Font, float Size, const char* Text)
{
    return ObservatoryTextWidth(Font, Size, Text);
}
void Text(ImDrawList* List, ImFont* Font, float Size, float X, float Y, ImU32 Colour, const char* Value)
{
    ObservatoryText(List, Font, Size, X, Y, Colour, Value);
}
void RightText(ImDrawList* List, ImFont* Font, float Size, float Right, float Y, ImU32 Colour, const char* Value)
{
    Text(List, Font, Size, Right - TextWidth(Font, Size, Value), Y, Colour, Value);
}
bool Hit(const char* Id, float X, float Y, float Width, float Height)
{
    ImGui::SetCursorScreenPos(ImVec2(X, Y));
    return ImGui::InvisibleButton(Id, ImVec2(Width, Height), ImGuiButtonFlags_EnableNav);
}
void Centred(ImDrawList* List, ImFont* Font, float Size, float X, float Y, ImU32 Colour, const char* Value)
{
    Text(List, Font, Size, X - TextWidth(Font, Size, Value) * 0.5f, Y, Colour, Value);
}
void QuickTile(ImDrawList* List, const EditorTypefaces& Faces, float X, float Y, float Width,
               const char* Id, const char* Label, const char* Glyph, bool& Value, bool Available, bool& Changed)
{
    const float Height = 106.0f;
    if (Available && Hit(Id, X, Y, Width, Height)) { Value = !Value; Changed = true; }
    List->AddRectFilled(ImVec2(X, Y), ImVec2(X + Width, Y + Height), IM_COL32(31, 31, 31, 255), 16.0f);
    List->AddRect(ImVec2(X, Y), ImVec2(X + Width, Y + Height),
                  Value ? IM_COL32(50, 50, 50, 255) : IM_COL32(73, 50, 50, 255), 16.0f);
    const ImVec2 Centre(X + Width * 0.5f, Y + 30.0f);
    List->AddCircleFilled(Centre, 15.5f, Value ? IM_COL32(105, 200, 132, 255) : IM_COL32(204, 118, 115, 255), 32);
    ConstructEditorGlyph(List, Centre, 7.0f, IM_COL32(29, 50, 32, 255), Glyph, 1.4f);
    Centred(List, Faces.Regular, 10.0f, Centre.x, Y + 58.0f, IM_COL32(200, 200, 200, 255), Label);
    Centred(List, Faces.Regular, 8.0f, Centre.x, Y + 79.0f,
            Value ? IM_COL32(105, 200, 132, 255) : IM_COL32(204, 118, 115, 255), Available ? (Value ? "ON" : "OFF") : "NOT PORTED");
}
}

SunPosition SolveSun(const SolarSettings& S, double Hour)
{
    // J2000 orbit and horizontal conversion match the reference's CelestialSolver.
    int Year = S.Year, Month = S.Month;
    if (Month <= 2) { --Year; Month += 12; }
    const int A = Year / 100, B = 2 - A + A / 4;
    const double Jd = std::floor(365.25 * (Year + 4716)) + std::floor(30.6001 * (Month + 1)) +
                      S.Day + B - 1524.5 + (Hour - S.TimeZone) / 24.0;
    const double D = Jd - 2451543.5, Rad = Pi / 180.0;
    const auto Wrap = [](double V) { V = std::fmod(V, 360.0); return V < 0 ? V + 360.0 : V; };
    const double Eccentricity = .016709 - 1.151e-9 * D, Mean = Wrap(356.047 + .9856002585 * D) * Rad;
    double E = Mean;
    for (int I = 0; I < 6; ++I) E -= (E - Eccentricity * std::sin(E) - Mean) / (1.0 - Eccentricity * std::cos(E));
    const double X = std::cos(E) - Eccentricity, Y = std::sqrt(1.0 - Eccentricity * Eccentricity) * std::sin(E);
    const double Longitude = std::atan2(Y, X) + (282.9404 + 4.70935e-5 * D) * Rad;
    const double Obliquity = (23.4393 - 3.563e-7 * D) * Rad;
    const double RA = std::atan2(std::sin(Longitude) * std::cos(Obliquity), std::cos(Longitude));
    const double Dec = std::asin(std::sin(Longitude) * std::sin(Obliquity));
    const double Lst = Wrap(280.46061837 + 360.98564736629 * (Jd - 2451545.0) + S.Longitude) * Rad;
    const double H = Lst - RA, L = S.Latitude * Rad;
    const double East = -std::cos(Dec) * std::sin(H);
    const double North = std::sin(Dec) * std::cos(L) - std::cos(Dec) * std::cos(H) * std::sin(L);
    const double Up = std::sin(Dec) * std::sin(L) + std::cos(Dec) * std::cos(H) * std::cos(L);
    return {std::atan2(North, East) / Rad, std::asin(std::clamp(Up, -1.0, 1.0)) / Rad};
}
void AdvanceSun(SolarSettings& S, double Seconds)
{
    if (!std::isfinite(Seconds) || Seconds < 0.0) return;
    const float OldTime = S.Time, OldAzimuth = S.AzimuthDegrees, OldElevation = S.ElevationDegrees;
    if (S.DayCycle) { S.Time = static_cast<float>(std::fmod(S.Time + Seconds * 24.0 / (std::max(.01f, S.DayDuration) * 3600.0), 24.0)); S.AutoPosition = true; }
    if (S.AutoPosition) { const auto Position = SolveSun(S, S.Time); S.AzimuthDegrees = static_cast<float>(Position.Azimuth); S.ElevationDegrees = static_cast<float>(Position.Elevation); }
    if (S.Time != OldTime || S.AzimuthDegrees != OldAzimuth || S.ElevationDegrees != OldElevation) ++S.Revision;
}

namespace
{
void Secondary(SolarSettings& S, const EditorTypefaces& Faces, float X, float W,
               const std::function<void()>& OpenFlare, bool& Changed)
{
    ImDrawList* List = ImGui::GetWindowDrawList();
    auto PanelText = [](ImDrawList* D, ImFont* F, float Size, float PX, float PY, ImU32 C, const char* T)
    { Text(D, F, Size * 1.2f, PX, PY, C, T); };
    auto PanelRight = [](ImDrawList* D, ImFont* F, float Size, float PX, float PY, ImU32 C, const char* T)
    { RightText(D, F, Size * 1.2f, PX, PY, C, T); };
    const float R = X + W;
    float Y = ImGui::GetCursorScreenPos().y;
    auto Title = [&](const char* Name, const char* Index, float Height) {
        List->AddRectFilled(ImVec2(X, Y), ImVec2(R, Y + Height), IM_COL32(35, 36, 30, 255), 24.f);
        PanelText(List, Faces.Regular, 12.f, X + 18.f, Y + 18.f, Ink, Name);
        PanelRight(List, Faces.Regular, 8.f, R - 18.f, Y + 19.f, Fine, Index);
    };
    auto Slider = [&](const char* Id, const char* Label, float& V, float Min, float Max, float Step, const char* Unit, const char* Format = "%.1f") {
        ImGui::SetCursorScreenPos(ImVec2(X + 18.f, Y)); ImGui::PushItemWidth(W - 36.f);
        bool Result = AuthoringSlider(Id, Label, V, Min, Max, Step, Unit, Format, W - 36.f);
        ImGui::PopItemWidth(); Y += 68.f; Changed |= Result; return Result;
    };
    if (S.Tab == 1)
    {
        Title("Solar day", "03 / EPHEMERIS", 618.f);
        PanelText(List, Faces.Regular, 9.f, X + 18.f, Y + 49.f, Fine, "Local time");
        char Time[16]; const int Minutes = (static_cast<int>(std::round(S.Time * 60.f)) + 1440) % 1440;
        std::snprintf(Time, sizeof Time, "%02d:%02d", Minutes / 60, Minutes % 60);
        PanelText(List, Faces.Light, 40.f, X + 18.f, Y + 73.f, Ink, Time);
        PanelRight(List, Faces.Regular, 9.f, R - 18.f, Y + 58.f, Fine, S.DayCycle ? "Cycle running" : S.AutoPosition ? "Time linked" : "Manual direction");
        const float GX = X + 27.f, GY = Y + 150.f, GW = W - 54.f, GH = 133.f;
        List->AddLine(ImVec2(GX, GY + GH * .5f), ImVec2(GX + GW, GY + GH * .5f), IM_COL32(227, 202, 153, 50));
        for (int I = 0; I < 3; ++I) {
            const float PY = GY + GH * (.5f + (I - 1) * .25f);
            List->AddLine(ImVec2(GX, PY), ImVec2(GX + GW, PY), IM_COL32(227, 202, 153, 25));
            PanelText(List, Faces.Regular, 7.f, GX - 4.f, PY - 4.f, Fine, I == 0 ? "60°" : I == 1 ? "0°" : "-60°");
        }
        for (int I = 0; I < 5; ++I) {
            const float PX = GX + GW * I / 4.f;
            PanelText(List, Faces.Regular, 7.f, PX - 6.f, GY + GH + 3.f, Fine, I == 0 ? "00:00" : I == 1 ? "06:00" : I == 2 ? "12:00" : I == 3 ? "18:00" : "24:00");
        }
        ImVec2 Prior{};
        double Rise = -1, Set = -1, Above = 0; double OldElevation = 0;
        for (int I = 0; I <= 96; ++I)
        {
            const double Hour = I / 4.0, Elevation = SolveSun(S, Hour).Elevation;
            const ImVec2 P(GX + GW * float(Hour / 24.0), GY + GH * float(.5 - Elevation / 240.0));
            if (I) {
                List->AddLine(Prior, P, Elevation >= 0 ? IM_COL32(239, 211, 151, 255) : IM_COL32(159, 153, 122, 150), 1.5f);
                if ((OldElevation < 0) != (Elevation < 0)) {
                    const double Crossing = (I - 1) / 4.0 + .25 * (-OldElevation) / (Elevation - OldElevation);
                    if (OldElevation < 0) Rise = Crossing; else Set = Crossing;
                }
                if (OldElevation >= 0 && Elevation >= 0) Above += .25;
                else if ((OldElevation >= 0) != (Elevation >= 0)) {
                    const double Fraction = std::clamp(-OldElevation / (Elevation - OldElevation), 0.0, 1.0);
                    Above += .25 * (OldElevation >= 0 ? Fraction : 1.0 - Fraction);
                }
            }
            Prior = P; OldElevation = Elevation;
        }
        const float Cursor = GX + GW * S.Time / 24.f;
        List->AddLine(ImVec2(Cursor, GY), ImVec2(Cursor, GY + GH), IM_COL32(232, 199, 139, 110));
        (void)Hit("##DayGraph", GX, GY, GW, GH);
        if (ImGui::IsItemActive() && ImGui::GetIO().MouseDown[0])
        { S.Time = std::clamp((ImGui::GetIO().MousePos.x - GX) / GW * 24.f, 0.f, 24.f); S.AutoPosition = true; AdvanceSun(S, 0); Changed = true; }
        const DiagramKeys DayKeys = ReadDiagramKeys();
        if (DayKeys.Home || DayKeys.Horizontal || DayKeys.Vertical) {
            S.Time = DayKeys.Home ? 12.f : std::clamp(S.Time + (DayKeys.Horizontal + DayKeys.Vertical) * .25f * DayKeys.Step, 0.f, 24.f);
            S.AutoPosition = true; AdvanceSun(S, 0); Changed = true;
        }
        auto Clock = [](double Value, char* Out, size_t Size) { if (Value < 0) { std::snprintf(Out, Size, "—"); return; } const int M = int(std::round(Value * 60)) % 1440; std::snprintf(Out, Size, "%02d:%02d", M / 60, M % 60); };
        char RiseText[16], SetText[16], Hours[16]; Clock(Rise, RiseText, sizeof RiseText); Clock(Set, SetText, sizeof SetText); std::snprintf(Hours, sizeof Hours, "%.1f h", Above);
        for (int I = 0; I < 3; ++I) { const float L = X + 18.f + I * (W - 36.f) / 3.f; PanelText(List, Faces.Regular, 9.f, L, Y + 320.f, Fine, I == 0 ? "Horizon rise" : I == 1 ? "Horizon set" : "Above horizon"); PanelText(List, Faces.Light, 20.f, L, Y + 339.f, Ink, I == 0 ? RiseText : I == 1 ? SetText : Hours); }
        PanelText(List, Faces.Regular, 10.f, X + 18.f, Y + 403.f, Fine, "Date/location solver · geometric 0° crossings, without refraction or");
        PanelText(List, Faces.Regular, 10.f, X + 18.f, Y + 419.f, Fine, "terrain. This path is a reference while direction is manual, not");
        PanelText(List, Faces.Regular, 10.f, X + 18.f, Y + 435.f, Fine, "recorded telemetry.");
        Y += 480.f;
        if (Slider("Time", "Local solar control time", S.Time, 0, 24, .01f, "h", "%.2f")) { S.AutoPosition = true; AdvanceSun(S, 0); }
        Slider("DayDuration", "Full day duration", S.DayDuration, .01f, 24, .01f, "h", "%.2f");
        Y += 14.f; Title("Time & location", "04 / COORDINATES", 425.f); Y += 49.f;
        ImGui::SetCursorScreenPos(ImVec2(X + 18.f, Y)); ImGui::SetNextItemWidth(W - 36.f);
        int Date[] = {S.Year, S.Month, S.Day};
        if (ImGui::InputInt3("##Date", Date)) { S.Year = std::clamp(Date[0], 1900, 2200); S.Month = std::clamp(Date[1], 1, 12); const int MonthDays[] = {31, ((S.Year % 4 == 0 && S.Year % 100 != 0) || S.Year % 400 == 0) ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; S.Day = std::clamp(Date[2], 1, MonthDays[S.Month-1]); S.AutoPosition = true; AdvanceSun(S, 0); Changed = true; }
        Y += 40.f;
        bool Location = Slider("Latitude", "Latitude", S.Latitude, -90, 90, .01f, "°", "%.2f");
        Location |= Slider("Longitude", "Longitude", S.Longitude, -180, 180, .01f, "°", "%.2f");
        Location |= Slider("TimeZone", "UTC offset", S.TimeZone, -12, 14, .5f, "h", "%.1f");
        if (Location) { S.AutoPosition = true; AdvanceSun(S, 0); }
        Y += 40.f;
    }
    else if (S.Tab == 2)
    {
        const float LightY = Y;
        PanelText(List, Faces.Regular, 12.f, X, Y, Ink, "Light source");
        PanelRight(List, Faces.Regular, 8.f, R, Y + 1.f, Fine, "05 / RADIANCE");
        PanelText(List, Faces.Regular, 9.f, X, Y + 31.f, Fine, "Sunlight intensity");
        char V[32]; std::snprintf(V, sizeof V, "%.1f ×", S.Multiplier);
        PanelText(List, Faces.Light, 30.f, X, Y + 48.f, Ink, V);
        Y += 100.f;
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if(AuthoringSlider("Intensity", "Sunlight multiplier", S.Multiplier, 0, 60, .1f, "×", "%.1f", W)) { S.Preset=-1; Changed=true; }
        Y = LightY + 165.f;
        PanelText(List, Faces.Regular, 10.f, X, Y, Fine, "Exhibit intensity multiplier, not a measured lux or irradiance value.");
        PanelText(List, Faces.Regular, 10.f, X, Y + 16.f, Fine, "Atmospheric transmission is applied by the renderer.");
        PanelText(List, Faces.Regular, 10.f, X, LightY + 210.f, Fine, "Colour source");
        ImGui::SetCursorScreenPos(ImVec2(X, LightY + 232.f)); ImGui::SetNextItemWidth(W);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(.13f,.12f,.1f,1));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.13f,.12f,.1f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.19f,.18f,.14f,1));
        const char* Sources[] = {"Temperature", "RGB"}; int Mode = S.UseRgb ? 1 : 0;
        if (ImGui::Combo("##ColourSource", &Mode, Sources, 2)) { S.UseRgb = Mode == 1; Changed = true; }
        ImGui::PopStyleColor(3); ImGui::PopStyleVar();
        PanelText(List, Faces.Regular, 10.f, X, LightY + 298.f, Fine, "Sun Tint");
        float DisplayTint[3];for(int I=0;I<3;++I)DisplayTint[I]=std::pow(std::clamp(S.Tint[I],0.f,1.f),1.f/2.2f);
        List->AddRectFilled(ImVec2(R-145.f,LightY+289.f),ImVec2(R,LightY+323.f),IM_COL32(35,34,30,255),17.f);
        List->AddRect(ImVec2(R-145.f,LightY+289.f),ImVec2(R,LightY+323.f),IM_COL32(81,72,54,180),17.f);
        List->AddCircleFilled(ImVec2(R-126.f,LightY+306.f),8.f,ImGui::ColorConvertFloat4ToU32(ImVec4(DisplayTint[0],DisplayTint[1],DisplayTint[2],1.f)));
        char Hex[16];std::snprintf(Hex,sizeof Hex,"#%02X%02X%02X",int(std::round(DisplayTint[0]*255)),int(std::round(DisplayTint[1]*255)),int(std::round(DisplayTint[2]*255)));
        Centred(List,Faces.Regular,10.f,R-68.f,LightY+300.f,Fine,Hex);PanelText(List,Faces.Regular,10.f,R-20.f,LightY+300.f,Fine,"↗");
        if(Hit("##SunTint",R-145.f,LightY+289.f,145.f,34.f))ImGui::OpenPopup("##SunTintPicker");
        if(ImGui::BeginPopup("##SunTintPicker")){if(ImGui::ColorPicker3("##SunTintColour",DisplayTint)){for(int I=0;I<3;++I)S.Tint[I]=std::pow(std::clamp(DisplayTint[I],0.f,1.f),2.2f);S.UseRgb=true;Changed=true;}ImGui::EndPopup();}
        Y = LightY + 350.f;
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (AuthoringSlider("Temperature", "Colour temperature", S.Temperature, 2000, 10000, 50, "K", "%.0f", W)) { S.UseRgb = false; Changed = true; }
        Y = LightY + 407.f; Title("Source spectrum", "06 / REFERENCE", 465.f);
        PanelText(List, Faces.Regular, 9.f, X + 18.f, Y + 58.f, Fine, "Thermal reference");
        std::snprintf(V, sizeof V, "%.0f K", S.Temperature); PanelText(List, Faces.Light, 42.f, X + 18.f, Y + 77.f, Ink, V);
        PanelRight(List, Faces.Regular, 9.f, R - 18.f, Y + 91.f, Fine, S.UseRgb ? "RGB source · reference only" : "Temperature source");
        double Max = 0; double Samples[65];
        for (int I=0; I<65; ++I) { const double L=(380.0+I*400.0/64)*1e-9; Samples[I]=1/(std::pow(L,5)*std::expm1(.01438776877/(L*S.Temperature))); Max=std::max(Max,Samples[I]); }
        for (int I=1; I<65; ++I) {
            const float L=X+32.f+(W-64.f)*(I-1)/64.f, RR=X+32.f+(W-64.f)*I/64.f;
            const float Y0=Y+270.f-float(Samples[I-1]/Max)*95.f,Y1=Y+270.f-float(Samples[I]/Max)*95.f;
            List->AddRectFilledMultiColor(ImVec2(L,std::max(Y0,Y1)),ImVec2(RR,Y+270.f),IM_COL32(133,151,170,50),IM_COL32(178,163,116,50),IM_COL32(178,163,116,50),IM_COL32(133,151,170,50));
            List->AddLine(ImVec2(L,Y0),ImVec2(RR,Y1),IM_COL32(240,221,186,255),1.5f);
        }
        const float PlotX=X+32.f, PlotW=W-64.f;
        const double Peak=2897771.955/std::max(1.f,S.Temperature);
        if(Peak>=380 && Peak<=780) {
            const float PX=PlotX+float((Peak-380)/400)*PlotW;
            for(float PY=Y+175.f;PY<Y+270.f;PY+=5.f)List->AddLine(ImVec2(PX,PY),ImVec2(PX,PY+2),IM_COL32(238,214,166,80));
            List->AddCircleFilled(ImVec2(PX,Y+175.f),2.5f,IM_COL32(240,212,157,255));
            Centred(List,Faces.Regular,7.f,PX,Y+160.f,Fine,"λ MAX");
        }
        const ImU32 SpectrumColours[]={IM_COL32(143,129,181,255),IM_COL32(130,167,213,255),IM_COL32(145,189,172,255),IM_COL32(214,199,126,255),IM_COL32(202,138,118,255)};
        for(int I=0;I<4;++I){const float L=PlotX+PlotW*I/4,RR=PlotX+PlotW*(I+1)/4;List->AddRectFilledMultiColor(ImVec2(L,Y+279),ImVec2(RR,Y+283),SpectrumColours[I],SpectrumColours[I+1],SpectrumColours[I+1],SpectrumColours[I]);}
        for(int I=0;I<5;++I){char Tick[8];std::snprintf(Tick,sizeof Tick,"%d",380+100*I);Centred(List,Faces.Regular,7.f,PlotX+PlotW*I/4,Y+289,Fine,Tick);}
        Centred(List,Faces.Regular,7.f,X+W*.5f,Y+303,Fine,"VISIBLE WAVELENGTH / nm");
        List->AddLine(ImVec2(X+18,Y+320),ImVec2(R-18,Y+320),IM_COL32(176,162,133,45));
        PanelText(List,Faces.Regular,9.f,X+18,Y+337,Fine,"Blackbody peak");PanelText(List,Faces.Regular,9.f,X+W*.52f,Y+337,Fine,"Visible window");
        std::snprintf(V,sizeof V,"%.0f nm",Peak);PanelText(List,Faces.Light,22.f,X+18,Y+357,Ink,V);PanelText(List,Faces.Light,22.f,X+W*.52f,Y+357,Ink,"380–780 nm");
        PanelText(List,Faces.Regular,10.f,X+18,Y+398,Fine,"Normalised thermal reference, not measured radiance. The");
        PanelText(List,Faces.Regular,10.f,X+18,Y+414,Fine,"renderer uses its existing RGB approximation; RGB mode uses");
        PanelText(List,Faces.Regular,10.f,X+18,Y+430,Fine,"your custom tint. The blackbody peak may fall outside the visible");
        PanelText(List,Faces.Regular,10.f,X+18,Y+446,Fine,"window.");
        Y += 480.f;
    }
    else
    {
        Title("Apparent solar disc", "07 / ANGULAR SIZE", 468.f);
        PanelText(List, Faces.Regular, 9.f, X + 18.f, Y + 48.f, Fine, "Angular diameter");
        char V[32]; std::snprintf(V, sizeof V, "%.2f°", S.RadiusDegrees * 2.f);
        PanelText(List, Faces.Light, 30.f, X + 18.f, Y + 62.f, Ink, V);
        const ImVec2 Centre(X+W*.5f, Y+178.f);
        for (float Radius : {25.f, 48.f, 66.f}) List->AddCircle(Centre, Radius, IM_COL32(232,199,139,35), 64);
        List->AddCircleFilled(Centre, 7.f + S.RadiusDegrees * 21.f, S.DiscEnabled ? IM_COL32(241,215,166,255) : IM_COL32(119,110,91,255), 48);
        (void)Hit("##DiscDiagram", X+18.f, Y+100.f, W-36.f, 130.f);
        if (ImGui::IsItemActive() && ImGui::GetIO().MouseDown[0]) {
            S.RadiusDegrees=std::clamp(S.RadiusDegrees+ImGui::GetIO().MouseDelta.x*.005f,.05f,2.f); Changed=true;
        }
        const DiagramKeys DiscKeys=ReadDiagramKeys();
        if (DiscKeys.Home || DiscKeys.Horizontal || DiscKeys.Vertical) {
            S.RadiusDegrees=DiscKeys.Home ? .27f : std::clamp(S.RadiusDegrees+(DiscKeys.Horizontal+DiscKeys.Vertical)*.01f*DiscKeys.Step,.05f,2.f); Changed=true;
        }
        Y += 275.f; float Diameter = S.RadiusDegrees * 2.f;
        if (Slider("Diameter", "Angular diameter", Diameter, .1f, 4.f, .01f, "°", "%.2f")) S.RadiusDegrees = Diameter * .5f;
        Slider("Glow", "Optical aureole", S.Glow, 0, 3, .01f, "×", "%.2f");
        PanelText(List, Faces.Regular, 10.f, X + 18.f, Y + 1.f, Fine, "Disc diagram is exaggerated for editing; the sky uses the actual");
        PanelText(List, Faces.Regular, 10.f, X + 18.f, Y + 17.f, Fine, "angular diameter. The Sun disc quick toggle remains in Overview.");
        Y += 72.f; Title("Connected optics", "08 / SUBCOMPONENT", 167.f);
        PanelText(List, Faces.Regular, 10.f, X + 18.f, Y + 52.f, Fine, "Lens flare is a separate optical subcomponent. Opening it does");
        PanelText(List, Faces.Regular, 10.f, X + 18.f, Y + 68.f, Fine, "not change sunlight or exposure.");
        List->AddRect(ImVec2(X+18.f,Y+94.f),ImVec2(R-18.f,Y+135.f),IM_COL32(94,81,57,180),18.f);
        if (OpenFlare && Hit("##OpenFlare", X + 18.f, Y + 94.f, W - 36.f, 41.f)) OpenFlare();
        Centred(List, Faces.Regular, 10.f, X + W*.5f, Y + 109.f, OpenFlare ? Accent : Fine,
                OpenFlare ? "Open Lens Flare" : "Lens Flare navigation unavailable");
        Y += 181.f;
    }
    ImGui::SetCursorScreenPos(ImVec2(X, Y)); ImGui::Dummy(ImVec2(W, 0.f));
}
}

void ConstructSolarInspector(SolarSettings& S, const std::function<void()>& OpenFlare)
{
    const EditorTypefaces Faces = EditorTypefaces::Resolve();
    ImDrawList* List = ImGui::GetWindowDrawList();
    const ImVec2 Start = ImGui::GetCursorScreenPos();
    const float X = Start.x, W = std::max(1.0f, ImGui::GetContentRegionAvail().x), R = X + W;
    float Y = Start.y;
    bool Changed = false;

    Text(List, Faces.Regular, 9.0f, X, Y + 2.0f, Fine, "ENVIRONMENT  /  02");
    Y += 30.0f;
    Text(List, Faces.Light, 32.0f, X, Y, Ink, "Sun");
    const float ResetW = 55.0f;
    if (Hit("##ResetSun", R - ResetW, Y + 1.0f, ResetW, 28.0f))
    {
        S.ElevationDegrees = 38.0f; S.AzimuthDegrees = 145.0f; S.Multiplier = 12.0f;
        S.Temperature = 5778.0f; S.RadiusDegrees = .27f; S.Glow = 1.f; S.DayCycle = false; S.AutoPosition = false; S.UseRgb = false; Changed = true;
    }
    List->AddRectFilled(ImVec2(R - ResetW, Y + 1.0f), ImVec2(R, Y + 29.0f), IM_COL32(35, 34, 28, 255), 14.0f);
    List->AddRect(ImVec2(R - ResetW, Y + 1.0f), ImVec2(R, Y + 29.0f), IM_COL32(70, 65, 48, 255), 14.0f);
    Centred(List, Faces.Regular, 9.0f, R - ResetW * 0.5f - 3.0f, Y + 9.0f, IM_COL32(213, 191, 149, 255), "Reset");
    List->AddLine(ImVec2(R - 12.0f, Y + 16.0f), ImVec2(R - 8.0f, Y + 12.0f), Fine);
    List->AddLine(ImVec2(R - 11.0f, Y + 12.0f), ImVec2(R - 8.0f, Y + 12.0f), Fine);
    List->AddLine(ImVec2(R - 8.0f, Y + 12.0f), ImVec2(R - 8.0f, Y + 15.0f), Fine);
    Y += 43.0f;
    Text(List, Faces.Regular, 11.0f, X, Y, Muted, "Directional light / position & colour.");
    Y += 34.0f;

    // Navigation changes only local authoring state; the host decides how to apply it.
    const char* Tabs[] = {"Overview", "Daylight", "Light & colour", "Disc & optics"};
    const float TabW = (W - 9.0f) / 4.0f;
    for (int I = 0; I < 4; ++I)
    {
        const float Left = X + I * (TabW + 3.0f);
        ImGui::PushID(I);
        if (Hit("##Tab", Left, Y, TabW, 33.f)) S.Tab = I;
        ImGui::PopID();
        if (I == S.Tab) List->AddRectFilled(ImVec2(Left, Y), ImVec2(Left + TabW, Y + 33.0f), IM_COL32(52, 51, 41, 255), 18.0f);
        Centred(List, Faces.Regular, 10.0f, Left + TabW * 0.5f, Y + 10.0f,
                I == S.Tab ? IM_COL32(231, 223, 200, 255) : IM_COL32(167, 157, 135, 255), Tabs[I]);
    }
    Y += 53.0f;
    if (S.Tab != 0)
    {
        ImGui::SetCursorScreenPos(ImVec2(X + 7.f, Y + 13.f));
        Secondary(S, Faces, X + 7.f, W - 14.f, OpenFlare, Changed);
        if (Changed) ++S.Revision;
        return;
    }

    // Diagram uses the source canvas' 640 x 420 coordinate system scaled to the available card width.
    const float CardY = Y, DiagramW = std::max(1.0f, W - 36.0f), DiagramH = DiagramW * 420.0f / 640.0f;
    const float DiagramX = X + 18.0f, DiagramY = Y + 43.0f;
    const float CardH = 43.0f + DiagramH + 38.0f;
    List->AddRectFilled(ImVec2(X, Y), ImVec2(R, Y + CardH), IM_COL32(35, 36, 30, 255), 24.0f);
    Text(List, Faces.Regular, 12.0f, X + 18.0f, Y + 18.0f, Ink, "Solar vector");
    RightText(List, Faces.Regular, 8.0f, R - 18.0f, Y + 19.0f, Fine, "01 / DIRECTION");
    const float Scale = DiagramW / 640.0f;
    const ImVec2 Centre(DiagramX + 320.0f * Scale, DiagramY + 210.0f * Scale);
    for (float Rad : {45.0f, 90.0f, 135.0f, 180.0f})
        List->AddCircle(Centre, Rad * Scale, Rad == 90.0f ? IM_COL32(125, 117, 94, 120) : IM_COL32(125, 117, 94, 42), 96, 1.0f);
    for (int I = 0; I < 24; ++I)
    {
        const float A = I * Pi * 2.0f / 24.0f;
        List->AddLine(ImVec2(Centre.x + std::cos(A) * 174.0f * Scale, Centre.y + std::sin(A) * 174.0f * Scale),
                      ImVec2(Centre.x + std::cos(A) * 180.0f * Scale, Centre.y + std::sin(A) * 180.0f * Scale), IM_COL32(232, 199, 139, 85));
    }
    const float Az = S.AzimuthDegrees * Pi / 180.0f, Dist = (90.0f - S.ElevationDegrees) * Scale;
    const ImVec2 Point(Centre.x + std::cos(Az) * Dist, Centre.y - std::sin(Az) * Dist);
    List->AddLine(Centre, Point, Accent, 1.2f);
    List->AddCircleFilled(Centre, 2.0f, IM_COL32(217, 202, 176, 255), 12);
    List->AddCircleFilled(Point, 10.0f, IM_COL32(232, 199, 139, 22), 24);
    List->AddCircle(Point, 10.0f, IM_COL32(232, 199, 139, 112), 24);
    List->AddCircleFilled(Point, 3.5f, IM_COL32(240, 214, 159, 255), 18);
    Centred(List, Faces.Regular, 8.0f, Centre.x, DiagramY + 4.0f, Fine, "+Y / N");
    Text(List, Faces.Regular, 8.0f, DiagramX + DiagramW - 32.0f, Centre.y, Fine, "+X / E");
    Text(List, Faces.Regular, 8.0f, DiagramX + 20.0f, Centre.y, Fine, "W");
    Centred(List, Faces.Regular, 8.0f, Centre.x, DiagramY + DiagramH - 9.0f, Fine, "S");
    Text(List, Faces.Regular, 8.0f, Centre.x + 98.0f * Scale, Centre.y - 14.0f * Scale, Fine, "0° HORIZON");
    Text(List, Faces.Regular, 8.0f, DiagramX + 12.0f, DiagramY + DiagramH - 18.0f, Fine, "DRAG VECTOR");
    Text(List, Faces.Regular, 9.0f, X + 18.0f, CardY + 51.0f, Fine, "Elevation");
    char Number[64]; std::snprintf(Number, sizeof(Number), "%.1f°", static_cast<double>(S.ElevationDegrees));
    Text(List, Faces.Light, 37.0f, X + 18.0f, CardY + 68.0f, Ink, Number);
    List->AddRectFilled(ImVec2(R - 80.0f, CardY + 50.0f), ImVec2(R - 18.0f, CardY + 79.0f), IM_COL32(48, 49, 40, 255), 15.0f);
    Centred(List, Faces.Regular, 9.0f, R - 52.0f, CardY + 59.0f, IM_COL32(228, 219, 189, 255), S.AutoPosition ? "Time linked" : "Manual");
    List->AddLine(ImVec2(R - 30.0f, CardY + 66.0f), ImVec2(R - 26.0f, CardY + 62.0f), Fine);
    List->AddLine(ImVec2(R - 29.0f, CardY + 62.0f), ImVec2(R - 26.0f, CardY + 62.0f), Fine);
    List->AddLine(ImVec2(R - 26.0f, CardY + 62.0f), ImVec2(R - 26.0f, CardY + 65.0f), Fine);
    (void)Hit("##SolarVector", DiagramX, DiagramY, DiagramW, DiagramH);
    if (ImGui::IsItemActive() && ImGui::GetIO().MouseDown[0])
    {
        const float Dx = (ImGui::GetIO().MousePos.x - Centre.x) / Scale;
        const float Dy = (Centre.y - ImGui::GetIO().MousePos.y) / Scale;
        if (std::hypot(Dx, Dy) > 1.0f) S.AzimuthDegrees = std::atan2(Dy, Dx) * 180.0f / Pi;
        S.ElevationDegrees = 90.0f - std::min(180.0f, std::hypot(Dx, Dy));
        S.AutoPosition = false; S.DayCycle = false; S.Preset=-1; Changed = true;
    }
    const DiagramKeys VectorKeys=ReadDiagramKeys();
    if (VectorKeys.Home || VectorKeys.Horizontal || VectorKeys.Vertical) {
        if (VectorKeys.Home) { S.AzimuthDegrees=145.f; S.ElevationDegrees=38.f; }
        else { S.AzimuthDegrees=std::clamp(S.AzimuthDegrees+VectorKeys.Horizontal*VectorKeys.Step,-180.f,180.f);
               S.ElevationDegrees=std::clamp(S.ElevationDegrees+VectorKeys.Vertical*VectorKeys.Step,-90.f,90.f); }
        S.AutoPosition=false; S.DayCycle=false; S.Preset=-1; Changed=true;
    }
    if (Hit("##SunMode", R - 80.f, CardY + 50.f, 62.f, 29.f))
    {
        S.AutoPosition = !S.AutoPosition;
        if (!S.AutoPosition) S.DayCycle = false; else AdvanceSun(S, 0);
        Changed = true;
    }
    const float FooterY = CardY + CardH - 33.0f;
    List->AddLine(ImVec2(X + 18.0f, FooterY), ImVec2(R - 18.0f, FooterY), IM_COL32(232, 199, 139, 18));
    Text(List, Faces.Regular, 9.0f, X + 18.0f, FooterY + 11.0f, Fine, "WORLD XYZ · Z UP");
    const float El = S.ElevationDegrees * Pi / 180.0f, ActualAz = S.AzimuthDegrees * Pi / 180.0f;
    std::snprintf(Number, sizeof(Number), "%.2f / %.2f / %.2f", std::cos(El) * std::cos(ActualAz), std::cos(El) * std::sin(ActualAz), std::sin(El));
    RightText(List, Faces.Regular, 9.0f, R - 18.0f, FooterY + 11.0f, Fine, Number);
    Y += CardH + 14.0f;

    const float Gap = 7.0f, KpiW = (W - Gap * 2.0f) / 3.0f;
    const char* Labels[] = {"Azimuth", "Source tint", "Sunlight"};
    const char* Notes[] = {"° / +X east", "source setting", "× multiplier"};
    char Values[3][32];
    std::snprintf(Values[0], sizeof(Values[0]), "%.1f", static_cast<double>(S.AzimuthDegrees));
    std::snprintf(Values[1], sizeof(Values[1]), "%.0f K", static_cast<double>(S.Temperature));
    std::snprintf(Values[2], sizeof(Values[2]), "%.1f", static_cast<double>(S.Multiplier));
    for (int I = 0; I < 3; ++I)
    {
        const float Left = X + I * (KpiW + Gap); const bool Light = I == 2;
        List->AddRectFilled(ImVec2(Left, Y), ImVec2(Left + KpiW, Y + 91.0f), Light ? IM_COL32(215, 219, 184, 255) : IM_COL32(38, 39, 32, 255), 15.0f);
        Text(List, Faces.Regular, 8.0f, Left + 10.0f, Y + 14.0f, Light ? IM_COL32(98, 107, 81, 255) : Fine, Labels[I]);
        Text(List, Faces.Light, 22.0f, Left + 10.0f, Y + 31.0f, Light ? IM_COL32(41, 46, 32, 255) : Ink, Values[I]);
        Text(List, Faces.Regular, 8.0f, Left + 10.0f, Y + 67.0f, Light ? IM_COL32(98, 107, 81, 255) : Fine, Notes[I]);
    }
    Y += 107.0f;
    const int Columns = W < 369.0f ? 2 : 3;
    const float TileW = (W - 9.0f * (Columns - 1)) / Columns;
    QuickTile(List, Faces, X, Y, TileW, "##Sunlight", "Sunlight", "Sun", S.Enabled, true, Changed);
    QuickTile(List, Faces, X + TileW + 9.0f, Y, TileW, "##SunDisc", "Sun disc", "Sun", S.DiscEnabled, true, Changed);
    QuickTile(List, Faces, Columns == 2 ? X : X + 2.0f * (TileW + 9.0f), Columns == 2 ? Y + 115.0f : Y,
              TileW, "##DayCycle", "Day cycle", "Sun", S.DayCycle, true, Changed);
    if (S.DayCycle && !S.AutoPosition) { S.AutoPosition = true; AdvanceSun(S, 0); Changed = true; }
    Y += Columns == 2 ? 237.0f : 122.0f;

    List->AddLine(ImVec2(X, Y), ImVec2(R, Y), IM_COL32(255, 255, 255, 19));
    Text(List, Faces.Regular, 12.0f, X, Y + 17.0f, Ink, "Direction controls");
    RightText(List, Faces.Regular, 8.0f, R, Y + 19.0f, Fine, "02 / AIM");
    ImGui::SetCursorScreenPos(ImVec2(X, Y + 46.0f));
    bool DirectionChanged = AuthoringSlider("Azimuth", "Azimuth", S.AzimuthDegrees, -180.0f, 180.0f, 0.1f, "°");
    DirectionChanged |= AuthoringSlider("Elevation", "Elevation", S.ElevationDegrees, -90.0f, 90.0f, 0.1f, "°");
    if (DirectionChanged) { S.AutoPosition = false; S.DayCycle = false; S.Preset=-1; Changed = true; }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.64f, 0.59f, 0.49f, 1.0f));
    ImGui::PushFont(Faces.Regular, 10.0f);
    ImGui::TextWrapped("Inner horizon ring: 0°. Centre: zenith. Outer region: below horizon.");
    ImGui::PopFont();
    ImGui::PopStyleColor();
    const ImVec2 PresetOrigin = ImGui::GetCursorScreenPos();
    const char* Presets[] = {"Noon", "Golden", "Dusk", "Night"};
    const float Elevations[] = {64.0f, 10.0f, 1.5f, -14.0f};
    const float Azimuths[] = {145.0f, 160.0f, 172.0f, 172.0f};
    const float PresetW = std::max(1.0f, (W - 18.0f) / 4.0f);
    for (int I = 0; I < 4; ++I)
    {
        const float Left = X + I * (PresetW + 6.0f);
        if (Hit(Presets[I], Left, PresetOrigin.y + 12.0f, PresetW, 34.0f))
        {
            S.ElevationDegrees = Elevations[I]; S.AzimuthDegrees = Azimuths[I];
            S.Multiplier = 12.0f; S.AutoPosition = false; S.DayCycle = false; S.Preset = I; Changed = true;
        }
        List->AddRectFilled(ImVec2(Left, PresetOrigin.y + 12.0f), ImVec2(Left + PresetW, PresetOrigin.y + 46.0f),
                            S.Preset==I ? IM_COL32(79, 69, 49, 255) : IM_COL32(37, 35, 28, 255), 15.0f);
        Centred(List, Faces.Regular, 10.0f, Left + PresetW * 0.5f, PresetOrigin.y + 23.0f, S.Preset==I ? Ink : Fine, Presets[I]);
    }
    ImGui::SetCursorScreenPos(ImVec2(X, PresetOrigin.y + 52.0f));
    ImGui::Dummy(ImVec2(W, 0.0f));
    if (Changed) ++S.Revision;
}
} // namespace Frontier
#endif
