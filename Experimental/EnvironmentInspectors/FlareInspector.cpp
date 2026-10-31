/*==========================================================================================================================================
                                                      FLAREINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 Camera-optics inspector with the reference's procedural flare composite.
#include "Experimental/EnvironmentInspectors/EnvironmentInspectors.h"
#include "Experimental/EnvironmentInspectors/EnvironmentControls.h"
#include "Experimental/EnvironmentInspectors/ObservatoryTypography.h"
#include "Engine/Interface/InspectorControls.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>
namespace Frontier::Experimental
{
namespace
{
constexpr ImU32 Ink=IM_COL32(222,218,227,255),Muted=IM_COL32(161,155,170,255),Fine=IM_COL32(145,140,156,255),Accent=IM_COL32(215,193,158,255);
float Width(ImFont* F,float S,const char* T){return ObservatoryTextWidth(F,S*1.2f,T);}
void Text(ImDrawList* D,ImFont* F,float S,float X,float Y,ImU32 C,const char* T){ObservatoryText(D,F,S*1.2f,X,Y,C,T);}
void Right(ImDrawList* D,ImFont* F,float S,float X,float Y,ImU32 C,const char* T){Text(D,F,S,X-Width(F,S,T),Y,C,T);}
void Centre(ImDrawList* D,ImFont* F,float S,float X,float Y,ImU32 C,const char* T){Text(D,F,S,X-Width(F,S,T)/2,Y,C,T);}
bool Hit(const char* Id,float X,float Y,float W,float H){ImGui::SetCursorScreenPos(ImVec2(X,Y));return ImGui::InvisibleButton(Id,ImVec2(std::max(1.f,W),H),ImGuiButtonFlags_EnableNav);}
void Card(ImDrawList* D,float X,float Y,float W,float H){D->AddRectFilled(ImVec2(X,Y),ImVec2(X+W,Y+H),IM_COL32(32,31,38,255),22);D->AddRect(ImVec2(X,Y),ImVec2(X+W,Y+H),IM_COL32(72,69,82,180),22);}
void Slider(float X,float& Y,float W,const char* Id,const char* Name,float& Value,float Min,float Max,float Step,const char* Unit,bool& Changed,const char* Format="%.2f")
{ImGui::SetCursorScreenPos(ImVec2(X,Y));Changed|=AuthoringSlider(Id,Name,Value,Min,Max,Step,Unit,Format,W);Y+=68;}
void Preset(FlareSettings& S)
{
    if(S.Preset>=4)return;
    const float Ghosts[]={6,4,3,0},Bright[]={.35f,.3f,.2f,0},Halo[]={.5f,.2f,.1f,1},Streak[]={1,1.5f,0,0};
    S.GhostCount=Ghosts[S.Preset];S.GhostBrightness=Bright[S.Preset];S.HaloBrightness=Halo[S.Preset];S.Streak=Streak[S.Preset];
    S.GhostShape=0;S.GhostSpacing=1;S.Anamorphic=S.Preset<2;S.WarmStreak=false;S.Burst=S.Preset==0||S.Preset==2;S.Rays=S.Preset==1?2:S.Preset==3?6:8;
}
void Preview(ImDrawList* D,const FlareSettings& S,float X,float Y,float W,float H)
{
    constexpr int PW=160,PH=70;
    static std::array<float,18> Key{};static bool Ready=false;static std::vector<ImU32> Image;
    const std::array<float,18> Next={S.PreviewX,S.PreviewY,S.Intensity,S.Spread,S.Chromatic,S.GhostCount,S.GhostBrightness,S.GhostSpacing,S.HaloRadius,S.HaloWidth,S.HaloBrightness,S.Rotation,S.Rays,S.Streak,S.Aperture,float(S.GhostShape),float(S.Preset),float((S.Anamorphic?1:0)+(S.WarmStreak?2:0)+(S.Burst?4:0))};
    if(!Ready||Next!=Key){const auto Pixels=RenderFlareImage(S,PW,PH);Image.resize(PW*PH);for(int I=0;I<PW*PH;++I){int C[3];for(int J=0;J<3;++J){const float V=std::max(0.f,Pixels[I*3+J]);C[J]=int(std::round(255*std::pow(V/(1+V),1/2.2f)));}Image[I]=IM_COL32(C[0],C[1],C[2],255);}Key=Next;Ready=true;}
    D->PushClipRect(ImVec2(X,Y),ImVec2(X+W,Y+H),true);
    for(int J=0;J<PH-1;++J)for(int I=0;I<PW-1;++I){const float L=X+W*I/(PW-1),R=X+W*(I+1)/(PW-1),T=Y+H*J/(PH-1),B=Y+H*(J+1)/(PH-1);D->AddRectFilledMultiColor(ImVec2(L,T),ImVec2(R,B),Image[J*PW+I],Image[J*PW+I+1],Image[(J+1)*PW+I+1],Image[(J+1)*PW+I]);}
    D->PopClipRect();
    const float Radius=13; const ImU32 Outside=IM_COL32(32,31,38,255);
    D->PathLineTo(ImVec2(X,Y));D->PathLineTo(ImVec2(X+Radius,Y));D->PathArcTo(ImVec2(X+Radius,Y+Radius),Radius,-1.5707963f,-3.1415926f,10);D->PathFillConvex(Outside);
    D->PathLineTo(ImVec2(X+W,Y));D->PathLineTo(ImVec2(X+W,Y+Radius));D->PathArcTo(ImVec2(X+W-Radius,Y+Radius),Radius,0,-1.5707963f,10);D->PathFillConvex(Outside);
    D->PathLineTo(ImVec2(X,Y+H));D->PathLineTo(ImVec2(X,Y+H-Radius));D->PathArcTo(ImVec2(X+Radius,Y+H-Radius),Radius,3.1415926f,1.5707963f,10);D->PathFillConvex(Outside);
    D->PathLineTo(ImVec2(X+W,Y+H));D->PathLineTo(ImVec2(X+W-Radius,Y+H));D->PathArcTo(ImVec2(X+W-Radius,Y+H-Radius),Radius,1.5707963f,0,10);D->PathFillConvex(Outside);
}
}
void ConstructFlareInspector(FlareSettings& S,const FlareExport& Export)
{
    const EditorTypefaces F=EditorTypefaces::Resolve();ImDrawList* D=ImGui::GetWindowDrawList();const ImVec2 Start=ImGui::GetCursorScreenPos();
    const float X=Start.x+9,W=std::max(1.f,ImGui::GetContentRegionAvail().x-18),R=X+W;float Y=Start.y+27;bool Changed=false;
    Text(D,F.Regular,9,X+2,Y,Fine,"ENVIRONMENT / CAMERA OPTICS");Text(D,F.Light,31,X+2,Y+27,Ink,"Lens flare");
    Text(D,F.Regular,12,X+2,Y+72,Muted,"Reflections, diffraction, and light across the");Text(D,F.Regular,12,X+2,Y+92,Muted,"sensor.");Y+=127;
    D->AddRectFilled(ImVec2(X+2,Y),ImVec2(X+86,Y+32),IM_COL32(38,38,39,255),16);D->AddRect(ImVec2(X+2,Y),ImVec2(X+86,Y+32),IM_COL32(75,75,76,255),16);
    D->AddCircleFilled(ImVec2(X+21,Y+16),4,S.Enabled?Ink:Muted);Text(D,F.Regular,10,X+30,Y+10,Ink,S.Enabled?"Enabled":"Disabled");if(Hit("##FlareEnabled",X+2,Y,84,32)){S.Enabled=!S.Enabled;Changed=true;}
    D->AddRectFilled(ImVec2(R-102,Y),ImVec2(R-2,Y+32),IM_COL32(38,38,39,255),16);D->AddRect(ImVec2(R-102,Y),ImVec2(R-2,Y+32),IM_COL32(75,75,76,255),16);Centre(D,F.Regular,10,R-52,Y+10,Export?Accent:Fine,"Export HDR ↗");
    if(Export&&Hit("##ExportFlare",R-102,Y,100,32)){const auto Pixels=RenderFlareImage(S);Export(Pixels,640,280);}Y+=52;
    const float CardY=Y;Card(D,X,Y,W,392);Text(D,F.Regular,9,X+20,Y+25,Fine,"01 / LIVE PROCEDURAL REFERENCE");Text(D,F.Regular,18,X+20,Y+53,Ink,"Optical composite");
    const float IX=X+20,IY=Y+93,IW=W-40,IH=IW*280/640;
    Preview(D,S,IX,IY,IW,IH);
    (void)Hit("##FlareSource",IX,IY,IW,IH);
    if(ImGui::IsItemActive()&&ImGui::GetIO().MouseDown[0]){const auto M=ImGui::GetIO().MousePos;S.PreviewX=std::clamp((M.x-IX)/IW,.06f,.94f);S.PreviewY=std::clamp((M.y-IY)/IH,.08f,.92f);Changed=true;}
    const DiagramKeys SourceKeys=ReadDiagramKeys();
    if(SourceKeys.Home||SourceKeys.Horizontal||SourceKeys.Vertical){S.PreviewX=SourceKeys.Home?.28f:std::clamp(S.PreviewX+SourceKeys.Horizontal*.02f*SourceKeys.Step,.06f,.94f);S.PreviewY=SourceKeys.Home?.43f:std::clamp(S.PreviewY-SourceKeys.Vertical*.02f*SourceKeys.Step,.08f,.92f);Changed=true;}
    char V[64];std::snprintf(V,sizeof V,"SOURCE %.2f / %.2f",S.PreviewX,S.PreviewY);Text(D,F.Regular,9,IX,IY+IH+20,Fine,V);
    D->AddRect(ImVec2(X+W-101,IY+IH+10),ImVec2(X+W-20,IY+IH+42),IM_COL32(88,85,96,200),16);Centre(D,F.Regular,10,X+W-60,IY+IH+20,Ink,"Reset source");if(Hit("##FlareReset",X+W-101,IY+IH+10,81,32)){S.PreviewX=.28f;S.PreviewY=.43f;Changed=true;}
    Text(D,F.Regular,11,IX,IY+IH+60,Muted,"Drag the source. Authoring preview stays visible when");Text(D,F.Regular,11,IX,IY+IH+79,Muted,"scene flare is disabled; scene flare follows the Sun, camera");Text(D,F.Regular,11,IX,IY+IH+98,Muted,"occlusion and cabin transmission. Display-mapped");Text(D,F.Regular,11,IX,IY+IH+117,Muted,"thumbnail · export retains linear HDR.");
    Y=CardY+411;const char* Tabs[]={"Compose","Ghosts","Halo","Rays & streaks"};const float TW=(W-9)/4;
    for(int I=0;I<4;++I){const float L=X+I*(TW+3);ImGui::PushID(I);if(Hit("##FlareTab",L,Y,TW,33))S.Tab=I;ImGui::PopID();if(S.Tab==I)D->AddRectFilled(ImVec2(L,Y),ImVec2(L+TW,Y+33),IM_COL32(61,54,68,255),17);Centre(D,F.Regular,10,L+TW/2,Y+11,S.Tab==I?Ink:Muted,Tabs[I]);}Y+=53;
    const float Base=Y;
    if(S.Tab==0)
    {
        Card(D,X,Y,W,420);Text(D,F.Regular,9,X+20,Y+25,Fine,"02 / SOURCE & PRESET");Text(D,F.Regular,18,X+20,Y+53,Ink,"Light stack");Text(D,F.Regular,11,X+20,Y+98,Muted,"Optical preset");
        const char* Names[]={"Cinematic","Anamorphic","Starburst","Halo","Custom layers"};ImGui::SetCursorScreenPos(ImVec2(X+20,Y+120));ImGui::SetNextItemWidth(W-40);ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,12);ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.08f,.08f,.1f,1));ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.08f,.08f,.1f,1));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.16f,.15f,.18f,1));if(ImGui::Combo("##FlarePreset",&S.Preset,Names,5)){Preset(S);Changed=true;}ImGui::PopStyleColor(3);ImGui::PopStyleVar();Y+=163;
        Slider(X+20,Y,W-40,"FlareIntensity","Intensity",S.Intensity,0,5,.01f,"×",Changed);Slider(X+20,Y,W-40,"FlareSpread","Spread",S.Spread,.25f,2,.01f,"×",Changed);Slider(X+20,Y,W-40,"FlareChromatic","Chromatic separation",S.Chromatic,0,1,.01f,"",Changed);Y=Base+434;
    }
    else if(S.Tab==1)
    {
        Card(D,X,Y,W,650);Text(D,F.Regular,9,X+20,Y+25,Fine,"03 / SENSOR-PLANE POSITIONS");Text(D,F.Regular,18,X+20,Y+53,Ink,"Internal reflections");
        const float GX=X+20,GY=Y+93,GW=W-40,GH=140;D->AddRectFilled(ImVec2(GX,GY),ImVec2(GX+GW,GY+GH),IM_COL32(10,12,16,255),14);
        const float PX=GX+GW*35/640, PY=GY+GH*30/300, PW=GW*570/640, PH=GH*230/300;
        D->AddRectFilled(ImVec2(PX,PY),ImVec2(PX+PW,PY+PH),IM_COL32(16,19,24,255),6);
        const ImVec2 C(PX+PW*.5f,PY+PH*.5f),Source(PX+PW*S.PreviewX,PY+PH*S.PreviewY);
        for(float L=PX;L<PX+PW;L+=5)D->AddLine(ImVec2(L,C.y),ImVec2(std::min(L+1.f,PX+PW),C.y),IM_COL32(186,197,218,24));
        for(float T=PY;T<PY+PH;T+=5)D->AddLine(ImVec2(C.x,T),ImVec2(C.x,std::min(T+1.f,PY+PH)),IM_COL32(186,197,218,24));
        D->PushClipRect(ImVec2(PX,PY),ImVec2(PX+PW,PY+PH),true);D->AddLine(Source,ImVec2(C.x-(Source.x-C.x)*2.8f,C.y-(Source.y-C.y)*2.8f),IM_COL32(180,166,196,100));D->AddCircleFilled(Source,3,Accent);D->AddCircleFilled(C,2,Muted);
        const int Count=std::clamp(int(std::round(S.GhostCount)),0,24);for(int I=0;I<Count;++I){const float K=1-(.6f+(I+1.f)/Count*(1.7f+S.GhostSpacing));const ImVec2 P(C.x+(Source.x-C.x)*K,C.y+(Source.y-C.y)*K);if(P.x>=PX&&P.x<=PX+PW&&P.y>=PY&&P.y<=PY+PH)D->AddCircle(P,4+I%4*2,IM_COL32(195,175,210,170),S.GhostShape>=3?S.GhostShape:32);}D->PopClipRect();
        Text(D,F.Regular,8,PX,GY+GH-13,Fine,"SOURCE → SENSOR CENTRE → REFLECTIONS");
        (void)Hit("##GhostSpacing",GX,GY,GW,GH);
        if(ImGui::IsItemActive()&&ImGui::GetIO().MouseDown[0]){S.GhostSpacing=std::clamp((ImGui::GetIO().MousePos.x-GX)/GW*3,0.f,3.f);S.Preset=4;Changed=true;}
        const DiagramKeys GhostKeys=ReadDiagramKeys();
        if(GhostKeys.Home||GhostKeys.Horizontal||GhostKeys.Vertical){S.GhostSpacing=GhostKeys.Home?1.f:std::clamp(S.GhostSpacing+(GhostKeys.Horizontal+GhostKeys.Vertical)*.05f*GhostKeys.Step,0.f,3.f);S.Preset=4;Changed=true;}
        Y+=245;Text(D,F.Regular,11,X+20,Y,Muted,"Positions follow the actual kernel’s optical axis; outlines are");Text(D,F.Regular,11,X+20,Y+18,Muted,"a geometry guide, not radiance or exact ghost sizes. Off-");Text(D,F.Regular,11,X+20,Y+36,Muted,"sensor centres are omitted.");Y+=74;
        Text(D,F.Regular,11,X+20,Y,Muted,"Ghost shape");Y+=22;
        const char* Shapes[]={"Round","Hexagon","Octagon"};int Shape=S.GhostShape==6?1:S.GhostShape==8?2:0;ImGui::SetCursorScreenPos(ImVec2(X+20,Y));ImGui::SetNextItemWidth(W-40);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,12);ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.08f,.08f,.1f,1));ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.08f,.08f,.1f,1));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.16f,.15f,.18f,1));
        if(ImGui::Combo("##GhostShape",&Shape,Shapes,3)){S.GhostShape=Shape==1?6:Shape==2?8:0;S.Preset=4;Changed=true;}ImGui::PopStyleColor(3);ImGui::PopStyleVar();Y+=42;
        bool Edited=false;Slider(X+20,Y,W-40,"GhostCount","Ghost count",S.GhostCount,0,24,1,"",Edited,"%.0f");Slider(X+20,Y,W-40,"GhostBrightness","Ghost brightness",S.GhostBrightness,0,2,.01f,"×",Edited);Slider(X+20,Y,W-40,"GhostSpacing","Ghost spacing",S.GhostSpacing,0,3,.01f,"×",Edited);if(Edited){S.Preset=4;Changed=true;}Y=Base+664;
    }
    else if(S.Tab==2)
    {
        Card(D,X,Y,W,475);Text(D,F.Regular,9,X+20,Y+25,Fine,"04 / RADIAL CROSS-SECTION");Text(D,F.Regular,18,X+20,Y+53,Ink,"Annular response");const float GX=X+20,GY=Y+93,GW=W-40,GH=140;D->AddRectFilled(ImVec2(GX,GY),ImVec2(GX+GW,GY+GH),IM_COL32(10,12,16,255),14);const float PlotScale=GW/640.f;const ImVec2 C(GX+220*PlotScale,GY+130*PlotScale);const float Radius=S.HaloRadius*(95.f/.6f)*PlotScale;D->AddCircle(C,58,IM_COL32(189,190,169,25));D->AddCircle(C,Radius,IM_COL32(229,213,171,130));D->AddCircle(C,std::max(0.f,Radius-S.HaloWidth*(95.f/.6f)*PlotScale),IM_COL32(229,213,171,50));D->AddCircle(C,Radius+S.HaloWidth*(95.f/.6f)*PlotScale,IM_COL32(229,213,171,50));D->AddLine(C,ImVec2(C.x+Radius,C.y),Accent);D->AddCircleFilled(ImVec2(C.x+Radius,C.y),3,Accent);std::snprintf(V,sizeof V,"%.2f",S.HaloRadius);Text(D,F.Light,20,GX+GW*.67f,GY+42,Accent,V);
        (void)Hit("##HaloRadius",GX,GY,GW,GH);
        if(ImGui::IsItemActive()&&ImGui::GetIO().MouseDown[0]){const auto M=ImGui::GetIO().MousePos;S.HaloRadius=std::clamp(std::hypot(M.x-C.x,M.y-C.y)/((95.f/.6f)*PlotScale),.01f,.6f);S.Preset=4;Changed=true;}
        const DiagramKeys HaloKeys=ReadDiagramKeys();
        if(HaloKeys.Home||HaloKeys.Horizontal||HaloKeys.Vertical){S.HaloRadius=HaloKeys.Home?.22f:std::clamp(S.HaloRadius+(HaloKeys.Horizontal+HaloKeys.Vertical)*.01f*HaloKeys.Step,.01f,.6f);S.Preset=4;Changed=true;}Y+=250;
        bool Edited=false;Slider(X+20,Y,W-40,"HaloRadius","Halo radius",S.HaloRadius,.01f,.6f,.01f,"",Edited);Slider(X+20,Y,W-40,"HaloWidth","Halo width",S.HaloWidth,.005f,.2f,.001f,"",Edited,"%.3f");Slider(X+20,Y,W-40,"HaloBrightness","Halo brightness",S.HaloBrightness,0,2,.01f,"×",Edited);if(Edited){S.Preset=4;Changed=true;}Y=Base+489;
    }
    else
    {
        Card(D,X,Y,W,495);Text(D,F.Regular,9,X+20,Y+25,Fine,"05 / LAYER MIXER");Text(D,F.Regular,18,X+20,Y+53,Ink,"Diffraction & streaks");Y+=100;const float Tile=(W-54)/3;
        bool* Values[]={&S.Anamorphic,&S.WarmStreak,&S.Burst};const char* Labels[]={"Anamorphic","Warm streak","Starburst"};for(int I=0;I<3;++I){const float L=X+20+I*(Tile+7);ImGui::PushID(I);if(Hit("##RayTile",L,Y,Tile,72)){*Values[I]=!*Values[I];S.Preset=4;Changed=true;}ImGui::PopID();D->AddRectFilled(ImVec2(L,Y),ImVec2(L+Tile,Y+72),IM_COL32(28,28,29,255),13);D->AddRect(ImVec2(L,Y),ImVec2(L+Tile,Y+72),IM_COL32(66,67,66,255),13);D->AddCircleFilled(ImVec2(L+Tile/2,Y+24),15,*Values[I]?IM_COL32(105,200,132,255):IM_COL32(204,118,115,255));Centre(D,F.Regular,9,L+Tile/2,Y+52,Muted,Labels[I]);}Y+=90;
        bool Edited=false;Slider(X+20,Y,W-40,"Rotation","Rotation",S.Rotation,0,360,1,"°",Edited,"%.0f");Slider(X+20,Y,W-40,"Rays","Ray pairs",S.Rays,2,12,1,"",Edited,"%.0f");Slider(X+20,Y,W-40,"Streak","Streak gain",S.Streak,0,3,.01f,"×",Edited);if(Edited){S.Preset=4;Changed=true;}Slider(X+20,Y,W-40,"Aperture","Aperture blades",S.Aperture,2,12,1,"",Changed,"%.0f");Y=Base+509;
    }
    if(Changed)++S.Revision;ImGui::SetCursorScreenPos(ImVec2(X,Y));ImGui::Dummy(ImVec2(W,0));
}
}
