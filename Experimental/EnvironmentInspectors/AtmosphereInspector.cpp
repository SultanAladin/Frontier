/*==========================================================================================================================================
                                                   ATMOSPHEREINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 Atmospheric density, transmission and medium instruments.
#include "Experimental/EnvironmentInspectors/EnvironmentInspectors.h"
#include "Experimental/EnvironmentInspectors/EnvironmentControls.h"
#include "Experimental/EnvironmentInspectors/ObservatoryTypography.h"
#include "Engine/Interface/InspectorControls.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::Experimental
{
std::array<double,3> AtmosphereTransmission(const AtmosphereSettings& S)
{
    constexpr double Ground=6360000, Atmosphere=6460000, Origin=Ground+100;
    const double Elevation=S.SunElevation*3.14159265358979323846/180, Up=std::sin(Elevation), Dot=Origin*Up;
    const double GroundDisc=Dot*Dot-(Origin*Origin-Ground*Ground);
    if(GroundDisc>0 && -Dot-std::sqrt(GroundDisc)>0) return {0,0,0};
    const double Step=(-Dot+std::sqrt(Dot*Dot-(Origin*Origin-Atmosphere*Atmosphere)))/64;
    constexpr double Rayleigh[]={5.802e-6,13.558e-6,33.1e-6}, Ozone[]={.650e-6,1.881e-6,.085e-6};
    std::array<double,3> Depth={0,0,0};
    for(int I=0;I<64;++I) {
        const double T=(I+.5)*Step, Z=Origin+Up*T, Horizontal=std::cos(Elevation)*T;
        const double H=std::hypot(Z,Horizontal)-Ground;
        const double R=std::exp(-H/std::max(1.f,S.RayleighHeight)), M=std::exp(-H/std::max(1.f,S.MieHeight));
        const double O=std::max(0.,1-std::abs(H-25000.)/15000.);
        for(int C=0;C<3;++C) Depth[C]+=(Rayleigh[C]*R*S.Rayleigh+4.40e-6*M*S.Mie+Ozone[C]*O*S.Ozone)*Step;
    }
    for(double& V:Depth)V=std::exp(-V);return Depth;
}
namespace
{
constexpr float Pi=3.14159265358979323846f;
constexpr ImU32 Ink=IM_COL32(231,235,223,255),Muted=IM_COL32(151,163,143,255),Fine=IM_COL32(129,147,118,255),Accent=IM_COL32(211,226,162,255);
float Width(ImFont* F,float S,const char* T){return ObservatoryTextWidth(F,S*1.2f,T);}
void Text(ImDrawList* D,ImFont* F,float S,float X,float Y,ImU32 C,const char* T){ObservatoryText(D,F,S*1.2f,X,Y,C,T);}
void Right(ImDrawList* D,ImFont* F,float S,float X,float Y,ImU32 C,const char* T){Text(D,F,S,X-Width(F,S,T),Y,C,T);}
void Centre(ImDrawList* D,ImFont* F,float S,float X,float Y,ImU32 C,const char* T){Text(D,F,S,X-Width(F,S,T)/2,Y,C,T);}
bool Hit(const char* Id,float X,float Y,float W,float H){ImGui::SetCursorScreenPos(ImVec2(X,Y));return ImGui::InvisibleButton(Id,ImVec2(std::max(1.f,W),H),ImGuiButtonFlags_EnableNav);}
void Card(ImDrawList* D,float X,float Y,float W,float H,ImU32 Colour=IM_COL32(30,38,29,255)){D->AddRectFilled(ImVec2(X,Y),ImVec2(X+W,Y+H),Colour,23);}
void Heading(ImDrawList* D,const EditorTypefaces& F,float X,float Y,float W,const char* A,const char* B){Text(D,F.Regular,12,X+18,Y+18,Ink,A);Right(D,F.Regular,8,X+W-18,Y+19,Fine,B);}
void Slider(float X,float& Y,float W,const char* Id,const char* Name,float& Value,float Min,float Max,float Step,const char* Unit,bool& Changed,const char* Format="%.2f")
{ImGui::SetCursorScreenPos(ImVec2(X,Y));Changed|=AuthoringSlider(Id,Name,Value,Min,Max,Step,Unit,Format,W);Y+=68;}
void Line(ImDrawList* D,float X,float Y,float W){D->AddLine(ImVec2(X,Y),ImVec2(X+W,Y),IM_COL32(151,169,135,30));}
void Switch(ImDrawList* D,const EditorTypefaces& F,float X,float Y,float W,const char* Id,const char* Label,bool& Value,bool& Changed)
{
    if(Hit(Id,X,Y,W,40)){Value=!Value;Changed=true;}
    Text(D,F.Regular,10,X,Y+14,Muted,Label);const ImVec2 P(X+W-29,Y+10);
    D->AddRectFilled(P,ImVec2(P.x+29,P.y+18),Value?Accent:IM_COL32(63,69,57,255),9);
    D->AddCircleFilled(ImVec2(P.x+(Value?20:9),P.y+9),6,IM_COL32(38,47,32,255));
}
}
void ConstructAtmosphereInspector(AtmosphereSettings& S,const AtmosphereActions& Actions)
{
    const EditorTypefaces F=EditorTypefaces::Resolve();ImDrawList* D=ImGui::GetWindowDrawList();
    const ImVec2 Start=ImGui::GetCursorScreenPos();const float X=Start.x+7,W=std::max(1.f,ImGui::GetContentRegionAvail().x-14),R=X+W;float Y=Start.y+11;bool Changed=false;
    Text(D,F.Regular,9,X,Y,Fine,"ENVIRONMENT / 03");Text(D,F.Light,31,X,Y+29,Ink,"Atmosphere");
    D->AddRectFilled(ImVec2(R-52,Y+31),ImVec2(R,Y+60),IM_COL32(34,39,31,255),15);D->AddRect(ImVec2(R-52,Y+31),ImVec2(R,Y+60),IM_COL32(71,82,62,180),15);
    D->AddCircleFilled(ImVec2(R-40,Y+45),4,S.Enabled?Accent:Muted);Text(D,F.Regular,10,R-32,Y+40,S.Enabled?Accent:Muted,S.Enabled?"Live":"Off");
    if(Hit("##AtmosPower",R-52,Y+31,52,29)){S.Enabled=!S.Enabled;Changed=true;}
    Text(D,F.Regular,11,X,Y+74,Muted,"Scattering, density & the visible sky.");Y+=109;
    const char* Tabs[]={"Overview","Medium","Light response","Baking"};const float TW=(W-9)/4;
    for(int I=0;I<4;++I){const float L=X+I*(TW+3);ImGui::PushID(I);if(Hit("##AtmosTab",L,Y,TW,33))S.Tab=I;ImGui::PopID();if(I==S.Tab)D->AddRectFilled(ImVec2(L,Y),ImVec2(L+TW,Y+33),IM_COL32(48,55,43,255),17);Centre(D,F.Regular,10,L+TW/2,Y+11,I==S.Tab?Ink:Muted,Tabs[I]);}Y+=53;
    if(S.Tab==0)
    {
        const float CX=X-8,CW=W+16, CY=Y;Card(D,CX,CY,CW,424,IM_COL32(22,29,22,255));D->AddRect(ImVec2(CX,CY),ImVec2(CX+CW,CY+424),IM_COL32(61,77,57,100),23);
        const float Scale=(CW-18)/640, GX=CX+9,GY=CY+62;
        auto Point=[&](float A,float B){return ImVec2(GX+A*Scale,GY+B*Scale);};
        const ImVec2 Center=Point(320,453); const float Radius=240*Scale;
        D->PushClipRect(ImVec2(CX+1,CY+55),ImVec2(CX+CW-1,CY+309),true);
        for(int I=60;I>=0;--I){const float Km=I*.5f,Density=std::exp(-Km*1000/std::max(1.f,S.RayleighHeight));const float Aerosol=std::exp(-Km*1000/std::max(1.f,S.MieHeight));const int Alpha=int(std::min(.6f,.025f+Density*S.Rayleigh*.19f+Aerosol*S.Mie*.08f)*255);D->AddCircle(Center,Radius+Km*4*Scale,IM_COL32(173,207,175,Alpha),128);}
        D->AddCircleFilled(Center,Radius,IM_COL32(20,26,20,255),128);
        for(int I=12;I>=0;--I){const float R0=Radius*I/12;D->AddCircleFilled(ImVec2(Center.x-Radius*.25f,Center.y-Radius*.38f),R0,IM_COL32(54,63,51,7),96);}
        D->AddCircle(Center,Radius,IM_COL32(180,197,153,85),128);
        for(int I=1;I<=3;++I) D->AddEllipse(Center,ImVec2(Radius*I/4,Radius),IM_COL32(172,186,163,28),0,96);
        for(int I=1;I<=3;++I) D->AddEllipse(Center,ImVec2(Radius,Radius*I/4),IM_COL32(172,186,163,28),0,96);
        const float ProbeRadius=Radius+S.ProbeAltitude/1000*4*Scale, A=-1.17f;const ImVec2 Probe(Center.x+std::cos(A)*ProbeRadius,Center.y+std::sin(A)*ProbeRadius);
        D->PathArcTo(Center,ProbeRadius,-2.4f,-.6f,64);D->PathStroke(IM_COL32(212,233,155,150),1.2f,0);
        D->AddLine(Probe,Point(555,(Probe.y-GY)/Scale-40),IM_COL32(212,233,155,145));D->AddLine(Point(555,(Probe.y-GY)/Scale-40),Point(595,(Probe.y-GY)/Scale-40),IM_COL32(212,233,155,145));
        D->AddCircle(Probe,9,IM_COL32(212,233,155,140));D->AddCircleFilled(Probe,3,Ink);D->AddLine(ImVec2(Probe.x-13,Probe.y),ImVec2(Probe.x+13,Probe.y),IM_COL32(212,233,155,90));D->AddLine(ImVec2(Probe.x,Probe.y-13),ImVec2(Probe.x,Probe.y+13),IM_COL32(212,233,155,90));
        char V[32];std::snprintf(V,sizeof V,"%.1f km",S.ProbeAltitude/1000);Right(D,F.Regular,10,CX+CW-35,Probe.y-34,Ink,V);Text(D,F.Regular,9,CX+29,CY+278,Muted,"GROUND / Z = 0");D->PopClipRect();
        (void)Hit("##AtmosColumn",CX+9,CY+62,CW-18,247);
        if(ImGui::IsItemActive()&&ImGui::GetIO().MouseDown[0]) {const ImVec2 M=ImGui::GetIO().MousePos;const float Dist=std::hypot(M.x-Center.x,M.y-Center.y);S.ProbeAltitude=std::clamp(std::round((Dist-Radius)/(4*Scale)*2)*500.f,0.f,30000.f);Changed=true;}
        const DiagramKeys ColumnKeys=ReadDiagramKeys();
        if(ColumnKeys.Home||ColumnKeys.Horizontal||ColumnKeys.Vertical){S.ProbeAltitude=ColumnKeys.Home?0.f:std::clamp(S.ProbeAltitude+(ColumnKeys.Horizontal+ColumnKeys.Vertical)*500.f*ColumnKeys.Step,0.f,30000.f);Changed=true;}
        Heading(D,F,CX,CY,CW,"Atmospheric column","01 / DENSITY SCAN");Text(D,F.Regular,9,CX+18,CY+58,Muted,"Relative molecular density");std::snprintf(V,sizeof V,"%.1f",100*std::exp(-S.ProbeAltitude/std::max(1.f,S.RayleighHeight)));Text(D,F.Light,42,CX+18,CY+75,Ink,V);Text(D,F.Regular,9,CX+18,CY+124,Muted,"% of sea level");
        D->AddRectFilled(ImVec2(CX+CW-109,CY+62),ImVec2(CX+CW-17,CY+92),IM_COL32(40,49,34,255),15);std::snprintf(V,sizeof V,"↗ %.1f km probe",S.ProbeAltitude/1000);Centre(D,F.Regular,9,CX+CW-63,CY+71,Accent,V);
        D->AddRectFilled(ImVec2(CX+CW-106,CY+242),ImVec2(CX+CW-13,CY+316),IM_COL32(37,47,34,255),14);Text(D,F.Regular,9,CX+CW-93,CY+255,Muted,"Aerosol density");std::snprintf(V,sizeof V,"%.2f%%",100*std::exp(-S.ProbeAltitude/std::max(1.f,S.MieHeight)));Text(D,F.Light,22,CX+CW-93,CY+277,Ink,V);
        float AltKm=S.ProbeAltitude/1000;float SY=CY+327;if((ImGui::SetCursorScreenPos(ImVec2(CX+18,SY)),AuthoringSlider("ProbeAltitude","Probe altitude",AltKm,0,30,.5f,"km","%.1f",CW-36))){S.ProbeAltitude=AltKm*1000;Changed=true;}
        Text(D,F.Regular,9,CX+18,CY+388,Muted,"Exaggerated shell diagram · relative density, not a rendered planet.");Text(D,F.Regular,9,CX+18,CY+403,Muted,"Probe changes this diagnostic only.");Y=CY+442;
        const float KW=(W-14)/3;const char* Names[]={"Rayleigh","Mie","Ozone"};const char* Units[]={"× molecules","× aerosols","× absorption"};
        for(int I=0;I<3;++I){const float L=X+I*(KW+7);D->AddRectFilled(ImVec2(L,Y),ImVec2(L+KW,Y+90),I==2?IM_COL32(211,223,165,255):IM_COL32(34,41,31,255),15);const ImU32 C=I==2?IM_COL32(44,53,36,255):Ink;Text(D,F.Regular,8,L+10,Y+13,I==2?IM_COL32(79,96,66,255):Fine,Names[I]);std::snprintf(V,sizeof V,"%.2f",I==0?S.Rayleigh:I==1?S.Mie:S.Ozone);Text(D,F.Light,23,L+10,Y+32,C,V);Text(D,F.Regular,8,L+10,Y+65,I==2?IM_COL32(79,96,66,255):Fine,Units[I]);}Y+=106;
        Card(D,X,Y,W,255);Heading(D,F,X,Y,W,"Sky-view capture","GPU / SCATTERING");
        char Revision[40];std::snprintf(Revision,sizeof Revision,"Lookup revision  %llu",static_cast<unsigned long long>(Actions.SkyRevision));Text(D,F.Regular,9,X+14,Y+52,Muted,Revision);
        Right(D,F.Regular,9,R-14,Y+52,Fine,"GPU / RGBA16F");const float PX=X+14,PY=Y+76,PW=W-28,PH=145;
        D->AddRectFilled(ImVec2(PX,PY),ImVec2(PX+PW,PY+PH),IM_COL32(14,17,21,255),12);
        if(Actions.DrawSkyPreview)Actions.DrawSkyPreview(D,PX,PY,PW,PH);else Text(D,F.Regular,10,PX+8,PY+PH-22,Muted,"GPU sky lookup requires a renderer connection.");Y+=270;
        Text(D,F.Regular,12,X,Y,Ink,"Medium recipe");Right(D,F.Regular,8,R,Y,Fine,"LIVE COEFFICIENTS");Y+=30;
        Slider(X,Y,W,"Rayleigh","Molecular scattering",S.Rayleigh,0,3,.01f,"×",Changed);Slider(X,Y,W,"Mie","Aerosol haze",S.Mie,0,6,.01f,"×",Changed);Slider(X,Y,W,"Ozone","Ozone absorption",S.Ozone,0,4,.01f,"×",Changed);
    }
    else if(S.Tab==1)
    {
        Text(D,F.Regular,14,X,Y+10,Ink,"How the air thins");Right(D,F.Regular,8,R,Y+14,Fine,"SCALE-HEIGHT COMPARATOR");Y+=55;
        Text(D,F.Regular,10,X,Y,IM_COL32(184,211,189,255),"Rayleigh / molecules");Right(D,F.Regular,10,R,Y,IM_COL32(222,207,168,255),"Mie / aerosols");Y+=21;Line(D,X,Y,W);Y+=17;
        const float GraphY=Y,GraphH=230;D->AddRectFilled(ImVec2(X,Y),ImVec2(R,Y+GraphH),IM_COL32(23,28,23,255));
        const float Mid=X+W/2, Bottom=Y+188, Top=Y+29, Plot=Bottom-Top;Text(D,F.Regular,9,Mid-3,Y+4,Fine,"km");
        for(int I=0;I<=4;++I){const float YY=Bottom-I*Plot/4;D->AddLine(ImVec2(X+13,YY),ImVec2(Mid-20,YY),IM_COL32(176,198,170,20));D->AddLine(ImVec2(Mid+20,YY),ImVec2(R-13,YY),IM_COL32(176,198,170,20));char V[8];std::snprintf(V,sizeof V,"%d",I*5);Centre(D,F.Regular,10,Mid,YY-4,Muted,V);}
        for(int K=0;K<2;++K){const float H=K==0?S.RayleighHeight:S.MieHeight,Sign=K==0?-1.f:1.f,Axis=Mid+Sign*20,Span=W*.36f;const ImU32 Col=K==0?IM_COL32(184,211,189,255):IM_COL32(222,207,168,255);ImVec2 Prev{};for(int I=0;I<=100;++I){const float Alt=I*200,YY=Bottom-Alt/20000*Plot,XX=Axis+Sign*std::exp(-Alt/std::max(1.f,H))*Span;ImVec2 P(XX,YY);if(I)D->AddLine(Prev,P,Col,1.2f);Prev=P;}const float PY=Bottom-H/20000*Plot,PX=Axis+Sign*std::exp(-1.f)*Span;D->AddLine(ImVec2(Axis,PY),ImVec2(PX,PY),Col);D->AddCircleFilled(ImVec2(PX,PY),6,IM_COL32(20,27,18,255));D->AddCircle(ImVec2(PX,PY),6,Col);D->AddCircleFilled(ImVec2(PX,PY),2,Col);char V[24];std::snprintf(V,sizeof V,"%.1f km",H/1000);if(K==0)Text(D,F.Light,16,X+20,Y+202,Col,V);else Right(D,F.Light,16,R-20,Y+202,Col,V);}
        Centre(D,F.Regular,8,Mid,Y+208,Fine,"1/e HEIGHT");
        (void)Hit("##HeightGraph",X,GraphY,W,GraphH);
        if(ImGui::IsItemActive()&&ImGui::GetIO().MouseDown[0]){const ImVec2 M=ImGui::GetIO().MousePos;float V=std::round((Bottom-M.y)/Plot*20000/100)*100;S.SelectedScaleHeight=M.x<Mid?0:1;if(S.SelectedScaleHeight==0)S.RayleighHeight=std::clamp(V,1000.f,16000.f);else S.MieHeight=std::clamp(V,500.f,8000.f);Changed=true;}
        const DiagramKeys HeightKeys=ReadDiagramKeys();
        if(HeightKeys.Horizontal)S.SelectedScaleHeight=HeightKeys.Horizontal<0?0:1;
        if(HeightKeys.Home||HeightKeys.Vertical){float& V=S.SelectedScaleHeight==0?S.RayleighHeight:S.MieHeight;V=HeightKeys.Home?(S.SelectedScaleHeight==0?8000.f:1200.f):std::clamp(V+HeightKeys.Vertical*100.f*HeightKeys.Step,S.SelectedScaleHeight==0?1000.f:500.f,S.SelectedScaleHeight==0?16000.f:8000.f);Changed=true;}
        Y+=GraphH+13;Line(D,X,Y,W);Y+=16;Slider(X,Y,W,"RayleighHeight","Rayleigh scale height",S.RayleighHeight,1000,16000,100,"m",Changed,"%.0f");Slider(X,Y,W,"MieHeight","Mie scale height",S.MieHeight,500,8000,100,"m",Changed,"%.0f");
        Text(D,F.Regular,10,X,Y,Muted,"Drag either ribbon to edit its scale height. Markers sit at 36.8%");Text(D,F.Regular,10,X,Y+16,Muted,"of sea-level density. Each ribbon is normalised separately.");Y+=59;
        Card(D,X,Y,W,250);Heading(D,F,X,Y,W,"Constituents","SEA-LEVEL MULTIPLIERS");Y+=54;Slider(X+18,Y,W-36,"Rayleigh","Rayleigh multiplier",S.Rayleigh,0,3,.01f,"×",Changed);Slider(X+18,Y,W-36,"Mie","Mie multiplier",S.Mie,0,6,.01f,"×",Changed);Slider(X+18,Y,W-36,"Ozone","Ozone multiplier",S.Ozone,0,4,.01f,"×",Changed);Y+=20;
        Line(D,X,Y,W);Y+=22;Text(D,F.Regular,14,X,Y,Ink,"Where light scatters");Right(D,F.Regular,8,R,Y+4,Fine,"DIRECTIONAL RESPONSE");Y+=32;
        const float Scale=W/640.f;const ImVec2 Origin(X+320*Scale,Y+165*Scale);
        for(float Radius : {45.f,90.f,135.f})D->AddCircle(Origin,Radius*Scale,IM_COL32(174,188,152,25),64);
        D->AddLine(ImVec2(X+40*Scale,Origin.y),ImVec2(X+600*Scale,Origin.y),IM_COL32(174,188,152,48));
        ImVec2 Prior{};const float G=std::clamp(S.MieG,-.95f,.95f);
        for(int I=0;I<=360;++I){const float Angle=I*2*Pi/360;const float Shape=std::pow((1-std::abs(G))*(1-std::abs(G))/(1+G*G-2*G*std::cos(Angle)),1.5f);const float Radius=std::sqrt(Shape)*135*Scale;const ImVec2 P(Origin.x+std::cos(Angle)*Radius,Origin.y+std::sin(Angle)*Radius);if(I)D->AddLine(Prior,P,IM_COL32(214,220,176,255),1.25f);Prior=P;}
        D->AddLine(ImVec2(X+40*Scale,Origin.y),ImVec2(X+175*Scale,Origin.y),IM_COL32(186,197,167,255));D->AddLine(ImVec2(X+162*Scale,Origin.y-8*Scale),ImVec2(X+175*Scale,Origin.y),IM_COL32(186,197,167,255));D->AddLine(ImVec2(X+162*Scale,Origin.y+8*Scale),ImVec2(X+175*Scale,Origin.y),IM_COL32(186,197,167,255));D->AddCircleFilled(Origin,3,Ink);
        Text(D,F.Regular,9,X+40*Scale,Y+320*Scale,Muted,"INCIDENT LIGHT →");Right(D,F.Regular,9,X+600*Scale,Y+320*Scale,Muted,"FORWARD →");Y+=340*Scale+15;Line(D,X,Y,W);Y+=13;
        Slider(X,Y,W,"MieG","Mie anisotropy",S.MieG,-.95f,.95f,.01f,"g",Changed);
        Text(D,F.Regular,10,X,Y,Muted,"Normalised phase reference, not radiance. Squared plot radius");Text(D,F.Regular,10,X,Y+16,Muted,"represents relative phase strength; positive g favours forward scattering.");Y+=54;
    }
    else if(S.Tab==2)
    {
        const float CY=Y;Card(D,X,Y,W,491);Heading(D,F,X,Y,W,"Light through air","TRANSMISSION");Text(D,F.Regular,9,X+18,Y+54,Muted,S.Enabled?(S.AirMass?"Direct sunlight · linked":"Attenuation bypassed · reference only"):"Atmosphere off · reference only");
        const auto T=AtmosphereTransmission(S);const ImVec2 Center(X+W/2,Y+201);const ImU32 Colours[]={IM_COL32(210,164,155,255),IM_COL32(182,204,156,255),IM_COL32(164,198,215,255)};
        for(int I=0;I<3;++I){const float Rad=105-I*16;D->PathArcTo(Center,Rad,Pi,2*Pi,64);D->PathStroke(IM_COL32(197,207,185,20),3.f,0);D->PathArcTo(Center,Rad,Pi,Pi+float(T[I])*Pi,64);D->PathStroke(Colours[I],3.f,0);}
        char V[32];std::snprintf(V,sizeof V,"%.1f%%",100*(T[0]*.2126+T[1]*.7152+T[2]*.0722));Centre(D,F.Light,35,Center.x,Y+151,Ink,V);Centre(D,F.Regular,9,Center.x,Y+199,Muted,"neutral-light transmission");
        Text(D,F.Regular,10,X+51,Y+219,Muted,"0%");Right(D,F.Regular,10,R-51,Y+219,Muted,"100%");Line(D,X+18,Y+251,W-36);
        const char* Labels[]={"Red","Green","Blue"};for(int I=0;I<3;++I){const float L=X+18+I*(W-36)/3;Text(D,F.Regular,9,L,Y+265,Muted,Labels[I]);std::snprintf(V,sizeof V,"%.1f%%",T[I]*100);Text(D,F.Light,20,L,Y+285,Colours[I],V);}Line(D,X+18,Y+321,W-36);
        std::snprintf(V,sizeof V,"Sun / %.1f°",S.SunElevation);Text(D,F.Regular,9,X+18,Y+336,Muted,V);Right(D,F.Regular,9,R-18,Y+336,Muted,"Observer / 100 m");
        Text(D,F.Regular,9,X+18,Y+358,Muted,"Isolated atmosphere, neutral RGB source. Centre value is");Text(D,F.Regular,9,X+18,Y+372,Muted,"luminance-weighted transmission; excludes clouds and source tint.");Line(D,X+18,Y+395,W-36);
        Switch(D,F,X+18,Y+400,W-36,"##AirMass","Apply air-mass attenuation",S.AirMass,Changed);Line(D,X+18,Y+439,W-36);Switch(D,F,X+18,Y+445,W-36,"##Multiple","Multiple scattering",S.MultipleScattering,Changed);Y=CY+510;
        Text(D,F.Regular,14,X,Y,Ink,"Sky & surface");Right(D,F.Regular,8,R,Y+4,Fine,"LINEAR RGB TINTS");Y+=34;
        const float CardW=(W-10)/2;
        for(int I=0;I<2;++I){
            const float L=X+I*(CardW+10);Card(D,L,Y,CardW,135);float* Color=I==0?S.SkyTint:S.GroundAlbedo;
            const ImVec2 Swatch(L+38,Y+39);D->AddCircleFilled(Swatch,28,IM_COL32(48,57,46,255),40);
            D->AddCircleFilled(Swatch,22,ImGui::ColorConvertFloat4ToU32(ImVec4(Color[0],Color[1],Color[2],1)),40);
            ImGui::PushID(I);if(Hit("##TintButton",L+10,Y+10,56,56))ImGui::OpenPopup("##TintPicker");
            if(ImGui::BeginPopup("##TintPicker")){if(ImGui::ColorPicker3("##Tint",Color))Changed=true;ImGui::EndPopup();}ImGui::PopID();
            Text(D,F.Regular,11,L+16,Y+83,Ink,I==0?"Sky Tint":"Ground Albedo");
        }Y+=151;
        Line(D,X,Y,W);Y+=14;Slider(X,Y,W,"SkyScale","Sky brightness",S.SkyScale,0,3,.01f,"×",Changed);
        Text(D,F.Regular,10,X,Y,Muted,"Ground albedo feeds the existing multiple-scattering closure; these");Text(D,F.Regular,10,X,Y+16,Muted,"wells are source colours, not a camera preview.");Y+=55;Line(D,X,Y,W);Y+=7;
        if(Hit("##HorizonDrawer",X,Y,W,35))S.HorizonShapingOpen=!S.HorizonShapingOpen;
        Text(D,F.Regular,11,X,Y+12,Accent,"Horizon shaping  ↗");Y+=42;
        if(S.HorizonShapingOpen){Slider(X,Y,W,"Horizon","Horizon glow",S.HorizonGlow,0,3,.01f,"×",Changed);Slider(X,Y,W,"White","White line",S.WhiteLine,0,3,.01f,"×",Changed);Switch(D,F,X,Y,W,"##CivilOnly","Civil twilight only",S.LineAtCivilOnly,Changed);Y+=48;}
        Line(D,X,Y,W);Y+=17;Text(D,F.Regular,14,X,Y,Ink,"Integration");Right(D,F.Regular,8,R,Y+4,Fine,"GPU LOOKUP QUALITY");Y+=34;Line(D,X,Y,W);Y+=14;
        Slider(X,Y,W,"TierSamples","Sky LUT samples",S.TierSamples,8,64,1,"samples",Changed,"%.0f");Y+=12;
    }
    else {
        Text(D,F.Regular,10,X,Y+2,Muted,"BAKING");Y+=30;
        Card(D,X,Y,W,249,IM_COL32(31,31,31,255));D->AddRect(ImVec2(X,Y),ImVec2(R,Y+249),IM_COL32(72,72,72,180),22);Heading(D,F,X,Y,W,"Atmosphere bake","");
        const float TileX=X+24,TileY=Y+69;D->AddRectFilled(ImVec2(TileX,TileY),ImVec2(TileX+118,TileY+106),IM_COL32(29,29,29,255),15);D->AddRect(ImVec2(TileX,TileY),ImVec2(TileX+118,TileY+106),IM_COL32(65,65,65,255),15);
        D->AddCircleFilled(ImVec2(TileX+59,TileY+25),15,S.UseBakedAtmosphere?IM_COL32(105,200,132,255):IM_COL32(204,118,115,255));Centre(D,F.Regular,10,TileX+59,TileY+54,Muted,"Use baked image");Centre(D,F.Regular,9,TileX+59,TileY+74,S.UseBakedAtmosphere?IM_COL32(105,200,132,255):IM_COL32(204,118,115,255),S.UseBakedAtmosphere?"ON":"OFF");
        if(Hit("##UseBaked",TileX,TileY,118,106)){S.UseBakedAtmosphere=!S.UseBakedAtmosphere;Changed=true;}
        const float BX=X+156;D->AddRect(ImVec2(BX,TileY),ImVec2(R-24,TileY+32),IM_COL32(83,83,83,200),11);Centre(D,F.Regular,10,(BX+R-24)/2,TileY+10,Actions.Bake?Ink:Fine,"Bake atmosphere");if(Actions.Bake&&Hit("##BakeAtmosphere",BX,TileY,R-24-BX,32))Actions.Bake();
        Text(D,F.Regular,10,BX,TileY+48,Muted,"Dirty-gated GPU bake · smooth");Text(D,F.Regular,10,BX,TileY+64,Muted,"dome lighting");Text(D,F.Regular,10,X+24,Y+190,Muted,"RGBA16F · transmittance + multi-scattering + sky view");Y+=265;
        Card(D,X,Y,W,353,IM_COL32(31,31,31,255));D->AddRect(ImVec2(X,Y),ImVec2(R,Y+353),IM_COL32(72,72,72,180),22);Heading(D,F,X,Y,W,"Baked atmosphere","");const char* Modes[]={"Air","Sky","Transmit","Sphere"};
        const float TW=(W-60)/4;for(int I=0;I<4;++I){const float L=X+24+I*(TW+4);ImGui::PushID(I);if(Hit("##BakePreviewTab",L,Y+68,TW,32))S.BakePreviewTab=I;ImGui::PopID();D->AddRect(ImVec2(L,Y+68),ImVec2(L+TW,Y+100),I==S.BakePreviewTab?IM_COL32(145,161,224,255):IM_COL32(75,75,75,255),12);Centre(D,F.Regular,10,L+TW/2,Y+78,Ink,Modes[I]);}
        D->AddRectFilled(ImVec2(X+24,Y+117),ImVec2(R-24,Y+262),IM_COL32(14,17,21,255),14);
        if(Actions.DrawBakedPreview)Actions.DrawBakedPreview(D,X+24,Y+117,W-48,145,S.BakePreviewTab);else Text(D,F.Regular,10,X+30,Y+242,Muted,"GPU preview unavailable until host integration.");Text(D,F.Regular,10,X+24,Y+280,Muted,"Decoded GPU pixels · scattering only. Solar disc, Moon,");Text(D,F.Regular,10,X+24,Y+296,Muted,"stars and fog remain separate.");Y+=368;
    }
    if(Changed)++S.Revision;ImGui::SetCursorScreenPos(ImVec2(X,Y));ImGui::Dummy(ImVec2(W,0));
}
}
