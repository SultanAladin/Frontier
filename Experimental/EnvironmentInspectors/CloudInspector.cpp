/*==========================================================================================================================================
                                                      CLOUDINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 Reference-led global cloud observatory and its CPU density atlas.
#include "Experimental/EnvironmentInspectors/EnvironmentInspectors.h"
#include "Experimental/EnvironmentInspectors/EnvironmentControls.h"
#include "Experimental/EnvironmentInspectors/ObservatoryTypography.h"
#include "Experimental/EnvironmentInspectors/CloudDiagnostic.h"
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
constexpr ImU32 Ink=IM_COL32(230,232,223,255), Muted=IM_COL32(153,159,146,255), Fine=IM_COL32(136,147,127,255), Accent=IM_COL32(211,223,165,255);
float Width(ImFont* Font,float Size,const char* Text) { return ObservatoryTextWidth(Font,Size*1.2f,Text); }
void Text(ImDrawList* D,ImFont* F,float Size,float X,float Y,ImU32 C,const char* T) { ObservatoryText(D,F,Size*1.2f,X,Y,C,T); }
void Right(ImDrawList* D,ImFont* F,float Size,float X,float Y,ImU32 C,const char* T) { Text(D,F,Size,X-Width(F,Size,T),Y,C,T); }
void Centre(ImDrawList* D,ImFont* F,float Size,float X,float Y,ImU32 C,const char* T) { Text(D,F,Size,X-Width(F,Size,T)/2,Y,C,T); }
bool Hit(const char* Id,float X,float Y,float W,float H)
{ ImGui::SetCursorScreenPos(ImVec2(X,Y));return ImGui::InvisibleButton(Id,ImVec2(std::max(1.f,W),H),ImGuiButtonFlags_EnableNav); }
void Card(ImDrawList* D,float X,float Y,float W,float H,ImU32 Colour=IM_COL32(34,36,33,255))
{ D->AddRectFilled(ImVec2(X,Y),ImVec2(X+W,Y+H),Colour,23); }
void Heading(ImDrawList* D,const EditorTypefaces& F,float X,float Y,float W,const char* A,const char* B)
{ Text(D,F.Regular,12,X+18,Y+18,Ink,A);Right(D,F.Regular,8,X+W-18,Y+19,Fine,B); }
void Rule(ImDrawList* D,float X,float Y,float W){D->AddLine(ImVec2(X,Y),ImVec2(X+W,Y),IM_COL32(110,118,104,33));}
void Dotted(ImDrawList* D,ImVec2 A,ImVec2 B,ImU32 Colour,float Dash,float Gap)
{
    const float DX=B.x-A.x,DY=B.y-A.y,Length=std::hypot(DX,DY);if(Length<=0)return;
    for(float T=0;T<Length;T+=Dash+Gap){const float E=std::min(T+Dash,Length);D->AddLine(ImVec2(A.x+DX*T/Length,A.y+DY*T/Length),ImVec2(A.x+DX*E/Length,A.y+DY*E/Length),Colour);}
}
void Slider(float X,float& Y,float W,const char* Id,const char* Name,float& Value,float Min,float Max,float Step,const char* Unit,bool& Changed,const char* Format="%.2f")
{
    ImGui::SetCursorScreenPos(ImVec2(X,Y));
    Changed |= AuthoringSlider(Id,Name,Value,Min,Max,Step,Unit,Format,W);
    Y+=68;
}
void Atlas(ImDrawList* D,const CloudSettings& S,float X,float Y,float W,float H)
{
    // Draw interpolated pixels as vertex colours; no backend-specific texture lifetime is needed.
    constexpr int PW=96,PH=48;
    static std::array<float,10> Key{}; static bool Ready=false; static std::vector<uint32_t> Pixels;
    const std::array<float,10> Next={S.Coverage,S.Density,S.Scale,S.Anvil,S.Detail,S.Base,S.Thickness,S.Ceiling,float(S.Type),S.Enabled?1.f:0.f};
    if(!Ready || Next!=Key) {Pixels=CloudAtlas(S,PW,PH,640.f/440.f);Key=Next;Ready=true;}
    D->PushClipRect(ImVec2(X,Y),ImVec2(X+W,Y+H),true);
    for(int J=0;J<PH-1;++J)for(int I=0;I<PW-1;++I)
    {
        const float L=X+W*I/(PW-1),R=X+W*(I+1)/(PW-1),T=Y+H*J/(PH-1),B=Y+H*(J+1)/(PH-1);
        D->AddRectFilledMultiColor(ImVec2(L,T),ImVec2(R,B),Pixels[J*PW+I],Pixels[J*PW+I+1],Pixels[(J+1)*PW+I+1],Pixels[(J+1)*PW+I]);
    }
    D->AddRectFilledMultiColor(ImVec2(X,Y),ImVec2(X+W,Y+H*.65f),IM_COL32(5,16,10,215),IM_COL32(5,16,10,215),IM_COL32(5,16,10,0),IM_COL32(5,16,10,0));
    for(int I=0;I<7;++I) Dotted(D,ImVec2(X+W*(30+I*96)/640.f,Y+10),ImVec2(X+W*(30+I*96)/640.f,Y+H-10),IM_COL32(187,215,197,36),1.2f,3.2f);
    for(int I=0;I<4;++I) Dotted(D,ImVec2(X+10,Y+H*(80+100*I)/440.f),ImVec2(X+W-10,Y+H*(80+100*I)/440.f),IM_COL32(187,215,197,36),1.2f,3.2f);
    const ImVec2 C(X+W*.5f,Y+H*.5f); D->AddLine(ImVec2(C.x-16,C.y),ImVec2(C.x+16,C.y),IM_COL32(228,235,215,130));
    D->AddLine(ImVec2(C.x,C.y-16),ImVec2(C.x,C.y+16),IM_COL32(228,235,215,130)); D->AddCircle(C,15,IM_COL32(212,230,168,100)); D->AddCircleFilled(C,2,Accent);
    D->PopClipRect();
    // Mask only the image's upper corners to the host background.
    const float Radius=23.f;
    D->PathLineTo(ImVec2(X,Y));D->PathLineTo(ImVec2(X+Radius,Y));
    D->PathArcTo(ImVec2(X+Radius,Y+Radius),Radius,-1.5707963f,-3.1415926f,12);
    D->PathFillConvex(ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)));
    D->PathLineTo(ImVec2(X+W,Y));D->PathLineTo(ImVec2(X+W,Y+Radius));
    D->PathArcTo(ImVec2(X+W-Radius,Y+Radius),Radius,0.f,-1.5707963f,12);
    D->PathFillConvex(ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)));
}
}
void ConstructCloudInspector(CloudSettings& S,const CloudActions& Actions)
{
    const EditorTypefaces F=EditorTypefaces::Resolve(); ImDrawList* D=ImGui::GetWindowDrawList();
    const ImVec2 Start=ImGui::GetCursorScreenPos();const float X=Start.x+7,W=std::max(1.f,ImGui::GetContentRegionAvail().x-14),R=X+W;
    float Y=Start.y+11; bool Changed=false;
    Text(D,F.Regular,9,X,Y,Fine,"ENVIRONMENT / 01");
    Text(D,F.Light,31,X,Y+29,Ink,"Clouds");
    D->AddRectFilled(ImVec2(R-52,Y+31),ImVec2(R,Y+60),IM_COL32(34,39,31,255),15);
    D->AddRect(ImVec2(R-52,Y+31),ImVec2(R,Y+60),IM_COL32(71,82,62,180),15);
    D->AddCircleFilled(ImVec2(R-40,Y+45),4,S.Enabled?Accent:Muted);
    Text(D,F.Regular,10,R-32,Y+40,S.Enabled?Accent:Muted,S.Enabled?"Live":"Off");
    if(Hit("##CloudPower",R-52,Y+31,52,29)){S.Enabled=!S.Enabled;Changed=true;}
    Text(D,F.Regular,11,X,Y+74,Muted,"Density field / shape & illumination.");
    Y+=109; const char* Tabs[]={"Overview","Formation","Rendering","Weather"}; const float TW=(W-9)/4;
    for(int I=0;I<4;++I) {const float L=X+I*(TW+3);ImGui::PushID(I);if(Hit("##CloudTab",L,Y,TW,33))S.Tab=I;ImGui::PopID();if(S.Tab==I)D->AddRectFilled(ImVec2(L,Y),ImVec2(L+TW,Y+33),IM_COL32(48,51,44,255),17);Centre(D,F.Regular,10,L+TW/2,Y+11,S.Tab==I?Ink:Muted,Tabs[I]);}
    Y+=53;
    if(S.Tab==0)
    {
        const float H=W*440.f/640.f, CardY=Y;
        Atlas(D,S,X,Y,W,H);
        // Footer and rounded outline identify this as a density diagnostic, not a lit render.
        D->AddRectFilled(ImVec2(X,Y+H),ImVec2(R,Y+H+36),IM_COL32(28,36,30,255),22,ImDrawFlags_RoundCornersBottom);
        Text(D,F.Regular,12,X+17,Y+16,Ink,"Density atlas"); Right(D,F.Regular,8,R-17,Y+18,Fine,"01 / FIELD");
        Text(D,F.Regular,9,X+17,Y+46,IM_COL32(208,216,197,255),"Coverage setting");
        char V[32];std::snprintf(V,sizeof V,"%.0f",S.Coverage*100);Text(D,F.Light,43,X+17,Y+63,Ink,V);Text(D,F.Regular,13,X+17+Width(F.Light,43,V)+3,Y+88,Ink,"%");
        const char* Types[]={"Stratus","Stratocumulus","Cumulus","Cumulonimbus","Altostratus","Cirrus"};const char* Type=Types[std::clamp(S.Type,0,5)];
        const float Tag=std::max(58.f,Width(F.Regular,9,Type)+20);D->AddRectFilled(ImVec2(R-17-Tag,Y+45),ImVec2(R-17,Y+71),IM_COL32(42,51,43,210),13);Centre(D,F.Regular,9,R-17-Tag*.5f,Y+53,Accent,Type);
        Text(D,F.Regular,10,X+15,Y+H-25,Ink,"XY / WORLD FIELD"); Right(D,F.Regular,10,R-15,Y+H-25,Ink,"DENSITY →");
        Text(D,F.Regular,9,X+15,Y+H+12,Muted,"Density diagnostic");Right(D,F.Regular,9,R-15,Y+H+12,Muted,"Not a lit sky render ↗");
        Y=CardY+H+50;
        const float KW=(W-14)/3; const char* Labels[]={"Layer base","Effective top","GPU cloud pass"};const char* Units[]={"km","km","ms · smoothed"};
        for(int I=0;I<3;++I){const float L=X+I*(KW+7);D->AddRectFilled(ImVec2(L,Y),ImVec2(L+KW,Y+91),I==2?IM_COL32(44,49,37,255):IM_COL32(36,38,34,255),15);Text(D,F.Regular,8,L+10,Y+13,Fine,Labels[I]);if(I==0)std::snprintf(V,sizeof V,"%.2f",S.Base/1000);else if(I==1)std::snprintf(V,sizeof V,"%.2f",std::min(S.Ceiling,S.Base+S.Thickness)/1000);else if(S.GpuMilliseconds>=0)std::snprintf(V,sizeof V,"%.2f",S.GpuMilliseconds);else std::snprintf(V,sizeof V,"—");Text(D,F.Light,23,L+10,Y+32,Ink,V);Text(D,F.Regular,8,L+10,Y+66,Fine,Units[I]);}
        Y+=107;Rule(D,X,Y,W);Y+=20;Text(D,F.Regular,12,X,Y,Ink,"Shape the field");Right(D,F.Regular,8,R,Y+1,Fine,"02 / MIX");Y+=29;
        Text(D,F.Regular,10,X,Y,Muted,"Coverage × density · drag the point or use arrow keys");Y+=26;
        const float GH=124;D->AddRectFilled(ImVec2(X,Y),ImVec2(R,Y+GH),IM_COL32(30,34,29,255),16);
        const float GX=X+17,GW=W-34,GY=Y+15,GridH=86;
        for(int I=0;I<5;++I)Dotted(D,ImVec2(GX,GY+GridH*I/4),ImVec2(GX+GW,GY+GridH*I/4),IM_COL32(170,182,161,40),1.4f,3.5f);
        for(int I=0;I<7;++I)Dotted(D,ImVec2(GX+GW*I/6,GY),ImVec2(GX+GW*I/6,GY+GridH),IM_COL32(170,182,161,35),1.2f,3.5f);
        const float PX=GX+GW*S.Coverage,PY=GY+GridH*(1-S.Density/4);
        D->AddRectFilled(ImVec2(GX,PY),ImVec2(PX,GY+GridH),IM_COL32(199,221,150,30));D->AddCircle(ImVec2(PX,PY),7,IM_COL32(206,222,170,130));D->AddCircleFilled(ImVec2(PX,PY),3,Accent);
        Text(D,F.Regular,7,GX,Y+GH-14,Fine,"CLEAR");Right(D,F.Regular,7,GX+GW,Y+GH-14,Fine,"OVERCAST");
        (void)Hit("##CloudMix",X,Y,W,GH);
        if(ImGui::IsItemActive()&&ImGui::GetIO().MouseDown[0]){const ImVec2 M=ImGui::GetIO().MousePos;S.Coverage=std::clamp((M.x-GX)/GW,0.f,1.f);S.Density=std::clamp((GY+GridH-M.y)/GridH*4,0.f,4.f);Changed=true;}
        const DiagramKeys MixKeys=ReadDiagramKeys();
        if(MixKeys.Home||MixKeys.Horizontal||MixKeys.Vertical){
            S.Coverage=MixKeys.Home?.55f:std::clamp(S.Coverage+MixKeys.Horizontal*.01f*MixKeys.Step,0.f,1.f);
            S.Density=MixKeys.Home?1.f:std::clamp(S.Density+MixKeys.Vertical*.05f*MixKeys.Step,0.f,4.f);Changed=true;
        }
        Y+=GH+14;Rule(D,X,Y,W);Y+=18;
        float Coverage=S.Coverage*100; if((ImGui::SetCursorScreenPos(ImVec2(X,Y)),AuthoringSlider("Coverage","Coverage",Coverage,0,100,1,"%","%.0f",W))){S.Coverage=Coverage/100;Changed=true;}Y+=68;
        Slider(X,Y,W,"Density","Density multiplier",S.Density,0,4,.05f,"×",Changed);
        if(Hit("##FormationLink",X,Y,W,40)) S.Tab=1; D->AddRect(ImVec2(X,Y),ImVec2(R,Y+40),IM_COL32(69,78,59,170),20);Centre(D,F.Regular,10,X+W/2,Y+14,Accent,"Formation & altitude  ↗");Y+=54;
    }
    else if(S.Tab==1)
    {
        const float CardY=Y; Card(D,X,Y,W,770);Heading(D,F,X,Y,W,"Layer anatomy","03 / PROFILE");
        Text(D,F.Regular,10,X+18,Y+49,Muted,"Formation");Y+=72;
        const char* Types[]={"Stratus","Stratocumulus","Cumulus","Cumulonimbus","Altostratus","Cirrus"};
        ImGui::SetCursorScreenPos(ImVec2(X+18,Y));ImGui::SetNextItemWidth(W-36);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,12);ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.06f,.09f,.07f,1));ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.06f,.09f,.07f,1));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.14f,.17f,.13f,1));
        if(ImGui::Combo("##CloudFormation",&S.Type,Types,6))Changed=true;ImGui::PopStyleColor(3);ImGui::PopStyleVar(); Y+=44;
        const float GX=X+36,GW=W-72, GY=Y,GH=155; const float Top=std::min(S.Ceiling,S.Base+S.Thickness),Depth=std::max(0.f,Top-S.Base);
        for(int I=0;I<5;++I){const float L=GY+GH*I/4;D->AddLine(ImVec2(GX,L),ImVec2(GX+GW,L),IM_COL32(211,223,194,23));char N[16];std::snprintf(N,sizeof N,"%.1f",(Top-Depth*I/4)/1000);Text(D,F.Regular,7,GX-4,L,Fine,N);}
        ImVec2 Prior{};for(int I=0;I<=64;++I){const float H=1-I/64.f;ImVec2 P(GX+32+CloudProfile(H,S.Type,S.Anvil)*(GW-100),GY+GH*I/64);if(I)D->AddLine(Prior,P,Accent,1.4f);Prior=P;}
        Right(D,F.Regular,7,GX+GW,GY-3,Fine,Top<S.Base+S.Thickness?"CEILING LIMITED":"WORLD Z / km");Text(D,F.Regular,7,GX+32,GY+GH+9,Fine,"NORMALISED DENSITY PROFILE");
        Y+=GH+26; Rule(D,X+18,Y,W-36);Y+=18;
        Slider(X+18,Y,W-36,"Base","Base altitude",S.Base,100,20000,100,"m",Changed,"%.0f");
        Slider(X+18,Y,W-36,"Thickness","Layer thickness",S.Thickness,100,6000,100,"m",Changed,"%.0f");
        Slider(X+18,Y,W-36,"Ceiling","Ceiling limit",S.Ceiling,4000,20000,100,"m",Changed,"%.0f");if(S.Base>S.Ceiling){S.Base=S.Ceiling;Changed=true;}
        Slider(X+18,Y,W-36,"Scale","Feature scale",S.Scale,.2f,3,.05f,"×",Changed);
        float Anvil=S.Anvil*100;if((ImGui::SetCursorScreenPos(ImVec2(X+18,Y)),AuthoringSlider("Anvil","Anvil shaping",Anvil,0,100,1,"%","%.0f",W-36))){S.Anvil=Anvil/100;Changed=true;}Y+=68;
        float Detail=S.Detail*100;if((ImGui::SetCursorScreenPos(ImVec2(X+18,Y)),AuthoringSlider("Detail","Edge erosion",Detail,0,100,1,"%","%.0f",W-36))){S.Detail=Detail/100;Changed=true;}Y+=68;
        Text(D,F.Regular,10,X+18,Y,Muted,"Altitude is world Z, not height above terrain. Profile is a density");Text(D,F.Regular,10,X+18,Y+16,Muted,"envelope, not a lit render.");Y=std::max(Y+49,CardY+785);
        if(Hit("##LayerStackDrawer",X,Y,W,36))S.LayerStackOpen=!S.LayerStackOpen;
        Text(D,F.Regular,10,X+3,Y+12,Fine,S.LayerStackOpen?"▾ Layer stack editor ↗":"▸ Layer stack editor ↗");Y+=47;
        if(S.LayerStackOpen){
            const float SY=Y, SW=W-36, SX=X+18, Scale=SW/340.f;
            Card(D,X,SY,W,425+210*Scale,IM_COL32(30,35,36,255));Heading(D,F,X,SY,W,"Layer thickness","NATIVE CLOUDS");
            char Number[32];std::snprintf(Number,sizeof Number,"%.2f km",S.Thickness/1000);Text(D,F.Light,28,SX,SY+51,Ink,Number);
            Right(D,F.Regular,9,R-18,SY+66,Fine,S.Base+S.Thickness>S.Ceiling?"CEILING LIMITED":"LAYER DEPTH");
            Text(D,F.Regular,10,SX,SY+94,Muted,"Lift the top of the stack to grow the layer.");
            const float GY=SY+119,GH=210*Scale;D->AddRectFilled(ImVec2(SX,GY),ImVec2(SX+SW,GY+GH),IM_COL32(25,29,32,255),14);
            auto Point=[&](float A,float B){return ImVec2(SX+A*Scale,GY+B*Scale);};
            for(int I=0;I<6;++I)Dotted(D,Point(15,26+I*28),Point(326,26+I*28),IM_COL32(118,131,139,32),2*Scale,5*Scale);
            const float Effective=std::max(0.f,std::min(S.Ceiling,S.Base+S.Thickness)-S.Base),Bottom=166,Top=Bottom-120*std::sqrt(Effective/6000);
            D->AddEllipseFilled(Point(215,Bottom+12),ImVec2(89*Scale,18*Scale),IM_COL32(12,15,18,80));
            if(Effective>0)for(int I=0;I<=18;++I){const float H=(I+.5f)/19,Profile=std::min(1.f,CloudProfile(H,S.Type,S.Anvil)), V=Bottom-(Bottom-Top)*H,RX=58+Profile*28;D->AddEllipseFilled(Point(214+std::sin(H*7)*4,V),ImVec2(RX*Scale,15*Scale),IM_COL32(189,213,223,18));D->AddEllipse(Point(214+std::sin(H*7)*4,V),ImVec2(RX*Scale,15*Scale),IM_COL32(184,209,222,36));D->AddEllipseFilled(Point(183,V-3),ImVec2(RX*.45f*Scale,11*Scale),IM_COL32(225,239,245,20));}
            D->AddLine(Point(58,Top),Point(58,Bottom),IM_COL32(168,199,215,255));D->AddLine(Point(50,Top),Point(71,Top),IM_COL32(183,216,233,255),2);D->AddLine(Point(50,Bottom),Point(71,Bottom),IM_COL32(183,216,233,255),2);
            Dotted(D,Point(73,Top),Point(300,Top),IM_COL32(202,231,242,160),3*Scale,4*Scale);Dotted(D,Point(73,Bottom),Point(300,Bottom),IM_COL32(202,231,242,74),3*Scale,4*Scale);
            D->AddRectFilled(Point(48,Top-5),Point(68,Top+5),IM_COL32(188,219,232,255),5);
            Text(D,F.Regular,8,SX+14*Scale,GY+(Top-13)*Scale,Fine,"TOP");Text(D,F.Regular,8,SX+14*Scale,GY+(Bottom+26)*Scale,Fine,"BASE");
            (void)Hit("##LayerStack",SX,GY,SW,GH);
            if(ImGui::IsItemActive()&&ImGui::GetIO().MouseDown[0]){const float P=std::clamp((166-(ImGui::GetIO().MousePos.y-GY)/Scale)/120.f,0.f,1.f);S.Thickness=std::clamp(std::round(6000*P*P),100.f,6000.f);Changed=true;}
            const DiagramKeys StackKeys=ReadDiagramKeys();if(StackKeys.Home||StackKeys.Horizontal||StackKeys.Vertical){S.Thickness=StackKeys.Home?100.f:std::clamp(S.Thickness+(StackKeys.Horizontal+StackKeys.Vertical)*100.f*StackKeys.Step,100.f,6000.f);Changed=true;}
            float Next=GY+GH+14;Text(D,F.Regular,9,SX,Next,Fine,"BASE ALTITUDE");Text(D,F.Regular,9,SX+SW*.52f,Next,Fine,"EFFECTIVE TOP");
            std::snprintf(Number,sizeof Number,"%.2f km",S.Base/1000);Text(D,F.Light,18,SX,Next+17,Ink,Number);std::snprintf(Number,sizeof Number,"%.2f km",std::max(S.Base,std::min(S.Ceiling,S.Base+S.Thickness))/1000);Text(D,F.Light,18,SX+SW*.52f,Next+17,Ink,Number);Next+=50;
            Slider(SX,Next,SW,"StackThickness","Thickness",S.Thickness,100,6000,1,"m",Changed,"%.0f");Text(D,F.Regular,9,SX,Next,Muted,"World Z altitude · stack is vertically exaggerated for editing.");Y=SY+440+GH;
        }
        Card(D,X,Y,W,365);Heading(D,F,X,Y,W,"Light response","04 / SCATTER");Y+=53;
        Slider(X+18,Y,W-36,"Anisotropy","Forward scattering",S.Anisotropy,-.8f,.8f,.01f,"g",Changed);
        float Absorption=S.Absorption*100;if((ImGui::SetCursorScreenPos(ImVec2(X+18,Y)),AuthoringSlider("Absorption","Light absorption",Absorption,0,100,1,"%","%.0f",W-36))){S.Absorption=Absorption/100;Changed=true;}Y+=68;
        float Powder=S.Powder*100;if((ImGui::SetCursorScreenPos(ImVec2(X+18,Y)),AuthoringSlider("Powder","Powder response",Powder,0,100,1,"%","%.0f",W-36))){S.Powder=Powder/100;Changed=true;}Y+=68;
        Slider(X+18,Y,W-36,"LightSteps","Sun-shadow taps",S.LightSteps,0,4,1,"",Changed,"%.0f");Y+=40;
    }
    else if(S.Tab==2)
    {
        const float CardY=Y;Card(D,X,Y,W,622);Heading(D,F,X,Y,W,"Render footprint","05 / BUDGET");Y+=54;
        Text(D,F.Regular,9,X+18,Y,Muted,"Full-frame pixel share");Text(D,F.Regular,9,X+W/2+5,Y,Muted,"Complete layer ray");
        char A[32],B[32];std::snprintf(A,sizeof A,"%.1f",S.Resolution*S.Resolution/100+.0001f);const float Depth=std::max(0.f,std::min(S.Ceiling,S.Base+S.Thickness)-S.Base);std::snprintf(B,sizeof B,"%.0f",std::min(96.f,std::ceil(S.Steps*std::max(1.f,Depth/1200))));
        Text(D,F.Light,34,X+18,Y+18,Ink,A);Text(D,F.Regular,9,X+18+Width(F.Light,34,A)+4,Y+42,Muted,"%");Text(D,F.Light,34,X+W/2+5,Y+18,Ink,B);Text(D,F.Regular,9,X+W/2+10+Width(F.Light,34,B),Y+42,Muted,"samples");Y+=82;
        const float FX=X+18,FW=W-36,FH=134;D->AddRectFilled(ImVec2(FX,Y),ImVec2(FX+FW,Y+FH),IM_COL32(20,26,20,255),12);D->AddRect(ImVec2(FX,Y),ImVec2(FX+FW,Y+FH),IM_COL32(80,95,69,150),12);
        for(float L=FX+17;L<FX+FW;L+=17)D->AddLine(ImVec2(L,Y),ImVec2(L,Y+FH),IM_COL32(120,144,102,17));for(float L=Y+17;L<Y+FH;L+=17)D->AddLine(ImVec2(FX,L),ImVec2(FX+FW,L),IM_COL32(120,144,102,17));
        const float RW=FW*S.Resolution/100,RH=FH*S.Resolution/100;D->AddRectFilled(ImVec2(FX,Y+FH-RH),ImVec2(FX+RW,Y+FH),IM_COL32(211,223,165,26),7);D->AddRect(ImVec2(FX,Y+FH-RH),ImVec2(FX+RW,Y+FH),IM_COL32(211,223,165,135),7);std::snprintf(A,sizeof A,"%.0f%% / axis",S.Resolution);Centre(D,F.Regular,9,FX+RW/2,Y+FH-RH/2-3,Accent,A);
        Y+=151;Text(D,F.Regular,10,FX,Y,Muted,"Cloud target · depth-aware reconstruction.");Y+=29;
        Slider(FX,Y,FW,"Resolution","Cloud resolution",S.Resolution,25,100,1,"%",Changed,"%.0f");Slider(FX,Y,FW,"SkySteps","Sky base samples",S.Steps,8,64,1,"",Changed,"%.0f");Slider(FX,Y,FW,"LocalSteps","Local base samples",S.LocalSteps,8,64,1,"",Changed,"%.0f");
        Text(D,F.Regular,10,FX,Y,Muted,"Thickness-aware sky rays use up to 96 samples.");Y=std::max(Y+28,CardY+636);
        Card(D,X,Y,W,390);Heading(D,F,X,Y,W,"Ground shadow field","06 / OCCLUSION");Y+=52;
        ImGui::SetCursorScreenPos(ImVec2(X+18,Y));if(ImGui::Checkbox("Cloud shadows",&S.Shadows))Changed=true;Y+=37;
        Slider(X+18,Y,W-36,"ShadowStrength","Shadow strength",S.ShadowStrength,0,1,.01f,"",Changed);Slider(X+18,Y,W-36,"ShadowSoftness","Shadow softness",S.ShadowSoftness,0,200,1,"m",Changed,"%.0f");Slider(X+18,Y,W-36,"ShadowDistance","Field span",S.ShadowDistance,1000,16000,100,"m",Changed,"%.0f");Slider(X+18,Y,W-36,"ShadowSamples","Shadow samples",S.ShadowSamples,4,12,1,"",Changed,"%.0f");Y+=40;
    }
    else
    {
        Card(D,X,Y,W,262,IM_COL32(22,23,22,255));D->AddRect(ImVec2(X,Y),ImVec2(R,Y+262),IM_COL32(69,72,65,130),20);Heading(D,F,X,Y,W,"Connected atmosphere","07 / FORCEFIELD");
        D->AddRectFilled(ImVec2(X+18,Y+48),ImVec2(R-18,Y+86),IM_COL32(20,25,20,255),13);
        Text(D,F.Regular,10,X+30,Y+61,Muted,"Follow shared wind");Right(D,F.Regular,10,R-30,Y+61,S.FollowWind?Accent:Muted,S.FollowWind?"On ↗":"Off");
        if(Hit("##FollowWind",X+18,Y+48,W-36,38)){S.FollowWind=!S.FollowWind;Changed=true;}
        D->AddRect(ImVec2(X+18,Y+98),ImVec2(R-18,Y+139),IM_COL32(69,78,59,120),18);Centre(D,F.Regular,10,X+W/2,Y+113,Actions.OpenWind?Accent:Fine,"Open Wind ForceField  ↗");
        if(Actions.OpenWind&&Hit("##OpenWind",X+18,Y+98,W-36,41)) Actions.OpenWind();
        Text(D,F.Regular,10,X+18,Y+155,Muted,"Wind is shared with precipitation and local media. This layer");Text(D,F.Regular,10,X+18,Y+171,Muted,"keeps its own follow switch.");
        D->AddRect(ImVec2(X+18,Y+191),ImVec2(R-18,Y+232),IM_COL32(69,78,59,120),18);Centre(D,F.Regular,10,X+W/2,Y+206,Actions.PlaceLocalCloud?Accent:Fine,"Place a local cloud  ↗");
        if(Actions.PlaceLocalCloud&&Hit("##PlaceCloud",X+18,Y+191,W-36,41)) Actions.PlaceLocalCloud();Y+=276;
        const float LightningY=Y;Card(D,X,Y,W,1110,IM_COL32(32,32,32,255));D->AddRect(ImVec2(X,Y),ImVec2(R,Y+1110),IM_COL32(66,66,66,180),20);
        Text(D,F.Regular,12,X+24,Y+27,Ink,"Lightning & thunder");
        Text(D,F.Regular,11,X+24,Y+72,Muted,"Opt-in weather effect · bright flashes. Cloud-spanning");Text(D,F.Regular,11,X+24,Y+90,Muted,"illumination and branching particle channels; exposure");Text(D,F.Regular,11,X+24,Y+108,Muted,"and sun settings are unchanged.");
        Y+=142;const float Tile=(W-60)/2;bool* Flags[]={&S.LightningEnabled,&S.LightningAuto};const char* Labels[]={"Lightning","Automatic strikes"};
        for(int I=0;I<2;++I){const float L=X+24+I*(Tile+12);ImGui::PushID(I);if(Hit("##Lightning",L,Y,Tile,64)){*Flags[I]=!*Flags[I];Changed=true;}ImGui::PopID();D->AddRectFilled(ImVec2(L,Y),ImVec2(L+Tile,Y+64),IM_COL32(29,29,29,255),13);D->AddRect(ImVec2(L,Y),ImVec2(L+Tile,Y+64),*Flags[I]?IM_COL32(55,89,62,255):IM_COL32(91,52,52,255),13);D->AddCircle(ImVec2(L+Tile/2,Y+19),7,*Flags[I]?IM_COL32(105,200,132,255):IM_COL32(204,118,115,255),20);D->AddLine(ImVec2(L+Tile/2,Y+9),ImVec2(L+Tile/2,Y+18),*Flags[I]?IM_COL32(105,200,132,255):IM_COL32(204,118,115,255),1.5f);Centre(D,F.Regular,9,L+Tile/2,Y+40,Muted,Labels[I]);}
        Y+=89;Text(D,F.Regular,11,X+24,Y,Muted,"Discharge");Y+=22;
        const char* Modes[]={"Cloud → ground","Inside cloud"};ImGui::SetCursorScreenPos(ImVec2(X+24,Y));ImGui::SetNextItemWidth(W-48);if(ImGui::Combo("##LightningMode",&S.LightningMode,Modes,2))Changed=true;Y+=52;
        Text(D,F.Regular,11,X+24,Y,Muted,"Cloud source");Y+=22;const char* Sources[]={"Auto · global, or local if no global","Global cloud layer","Local cloud volume"};ImGui::SetCursorScreenPos(ImVec2(X+24,Y));ImGui::SetNextItemWidth(W-48);if(ImGui::Combo("##LightningSource",&S.LightningCloudTarget,Sources,3))Changed=true;Y+=41;
        const float ActionW=(W-60)/2;for(int I=0;I<2;++I){const float L=X+24+I*(ActionW+12);const auto& Action=I==0?Actions.TriggerLightning:Actions.ToggleThunder;D->AddRect(ImVec2(L,Y),ImVec2(L+ActionW,Y+32),IM_COL32(80,80,80,200),12);Centre(D,F.Regular,10,L+ActionW/2,Y+10,Action?Ink:Fine,I==0?"Strike ahead":"Enable thunder sound");ImGui::PushID(I);if(Action&&Hit("##LightningAction",L,Y,ActionW,32))Action();ImGui::PopID();}Y+=48;
        D->AddRect(ImVec2(X+24,Y),ImVec2(R-24,Y+32),IM_COL32(80,80,80,200),12);Centre(D,F.Regular,10,X+W/2,Y+10,Actions.OpenParticleEditor?Ink:Fine,"Author lightning in Particle Editor");if(Actions.OpenParticleEditor&&Hit("##LightningEditor",X+24,Y,W-48,32))Actions.OpenParticleEditor();Y+=49;
        Slider(X+24,Y,W-48,"LightningDistance","Approx. strike distance",S.LightningDistance,50,6000,50,"m",Changed,"%.0f");
        Slider(X+24,Y,W-48,"LightningMin","Minimum interval",S.LightningIntervalMin,2,60,.5f,"s",Changed,"%.1f");S.LightningIntervalMax=std::max(S.LightningIntervalMin,S.LightningIntervalMax);
        Slider(X+24,Y,W-48,"LightningMax","Maximum interval",S.LightningIntervalMax,2,60,.5f,"s",Changed,"%.1f");S.LightningIntervalMin=std::min(S.LightningIntervalMin,S.LightningIntervalMax);
        Slider(X+24,Y,W-48,"LightningSpread","Horizontal cloud channel span",S.LightningSpread,400,10000,100,"m",Changed,"%.0f");
        Slider(X+24,Y,W-48,"LightningBrightness","Bolt brightness",S.LightningBrightness,0,3,.05f,"×",Changed);
        Slider(X+24,Y,W-48,"LightningGlow","Cloud flash strength",S.LightningCloudGlow,0,3,.05f,"×",Changed);
        Slider(X+24,Y,W-48,"LightningRadius","Cloud flash radius",S.LightningRadius,200,6000,100,"m",Changed,"%.0f");Y=std::max(Y+25,LightningY+1124);
    }
    if(Changed)++S.Revision;
    ImGui::SetCursorScreenPos(ImVec2(X,Y));ImGui::Dummy(ImVec2(W,0));
}
}
