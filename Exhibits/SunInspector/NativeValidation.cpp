/*==========================================================================================================================================
                                                        VALIDATION.CPP
==========================================================================================================================================*/
// 🧪 Exercise real ImGui input and rasterize inspector draw commands without a window system.
#include "Experimental/EnvironmentInspectors/EnvironmentInspectors.h"
#include "Experimental/EnvironmentInspectors/SolarInspector.h"
#include "Experimental/EnvironmentInspectors/CloudDiagnostic.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include <imgui.h>
#include <stb_image_write.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cmath>
using namespace Frontier::Experimental;
namespace
{
void Require(bool Condition,const char* Message) { if(!Condition) throw std::runtime_error(Message); }
void Save(const std::filesystem::path& Path) {
    constexpr int W=380,H=900; std::vector<unsigned char> Pixels(W*H*3,20);
    const auto* Data=ImGui::GetDrawData(); Require(Data&&Data->CmdListsCount,"missing draw commands");
    for(const auto* List:Data->CmdLists) FrontierExhibit::RasterizeCommands(List,Pixels.data(),W,H,Data->DisplayPos,Data->FramebufferScale);
    Require(stbi_write_png(Path.string().c_str(),W,H,3,Pixels.data(),W*3)!=0,"could not save capture");
}
}
int main(int Argc,char** Argv)
{
    try {
        Require(Argc==3,"expected EngineContent and evidence directory");
        const std::filesystem::path Content=std::filesystem::absolute(Argv[1]), Output=std::filesystem::absolute(Argv[2]);
        std::filesystem::create_directories(Output);
        ImGui::CreateContext(); auto& Io=ImGui::GetIO();
        Io.DisplaySize=ImVec2(380,900);Io.DeltaTime=1.f/60;Io.IniFilename=nullptr;
        Io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth=ImGui::GetPlatformIO().Renderer_TextureMaxHeight=4096;
        Require(Io.Fonts->AddFontFromFileTTF((Content/"FontArchives/DMSans/DMSans-Regular.ttf").string().c_str(),16)!=nullptr,"DM Sans missing");
        auto& Style=ImGui::GetStyle();Style.WindowPadding=ImVec2(12,12);Style.Colors[ImGuiCol_WindowBg]=ImVec4(.075f,.08f,.07f,1);Style.Colors[ImGuiCol_ChildBg]=Style.Colors[ImGuiCol_WindowBg];
        SolarSettings Sun; CloudSettings Cloud; AtmosphereSettings Atmo; FlareSettings Flare;
        int Panel=0; bool OpenFlare=false, OpenWind=false, BakeRequested=false, TestPreviews=false; int SkyDraws=0, BakedDraws=0;
        float ScrollRequest=-1.f, ScrollMax=0.f, ActualScroll=0.f; std::vector<float> Exported; int ExportWidth=0,ExportHeight=0;
        auto Render=[&]() {
            ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(380,900));
            ImGui::Begin("Environment",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
            if(ScrollRequest>=0.f) ImGui::SetScrollY(ScrollRequest);
            ScrollMax=ImGui::GetScrollMaxY();ActualScroll=ImGui::GetScrollY();
            if(Panel==0) ConstructSolarInspector(Sun,[&](){OpenFlare=true;});
            else if(Panel==1) { CloudActions Actions; Actions.OpenWind=[&](){OpenWind=true;}; ConstructCloudInspector(Cloud,Actions); }
            else if(Panel==2) { AtmosphereActions Actions; Actions.Bake=[&](){BakeRequested=true;};
                if(TestPreviews) {Actions.DrawSkyPreview=[&](ImDrawList*,float,float,float,float){++SkyDraws;}; Actions.DrawBakedPreview=[&](ImDrawList*,float,float,float,float,int){++BakedDraws;};}
                ConstructAtmosphereInspector(Atmo,Actions); }
            else ConstructFlareInspector(Flare,[&](const std::vector<float>& Pixels,int W,int H){Exported=Pixels;ExportWidth=W;ExportHeight=H;});
            ImGui::End();ImGui::Render();FrontierExhibit::AcknowledgeTextures();
        };
        auto Click=[&](float X,float Y){Io.AddMousePosEvent(X,Y);Io.AddMouseButtonEvent(0,true);Render();Io.AddMouseButtonEvent(0,false);Render();};
        auto Drag=[&](float X,float Y,float X2,float Y2){Io.AddMousePosEvent(X,Y);Io.AddMouseButtonEvent(0,true);Render();Io.AddMousePosEvent(X2,Y2);Render();Io.AddMouseButtonEvent(0,false);Render();};
        Render();Render();Save(Output/"sun-overview.png");
        for(int I=1;I<4;I++) {Sun.Tab=I;Render();Render();Save(Output/("sun-"+std::to_string(I)+".png"));}
        Sun.Tab=0;Render();Render();
        const float PrevElevation=Sun.ElevationDegrees;Drag(190,255,210,290);Require(std::fabs(Sun.ElevationDegrees-PrevElevation)>1,"sun diagram drag did not update");
        Click(75,620);Require(!Sun.Enabled,"sunlight toggle failed");
        Click(260,620);Require(!Sun.DiscEnabled,"disc toggle failed");
        Click(330,65);Require(std::fabs(Sun.ElevationDegrees-38.f)<.01f,"sun reset failed");
        Sun.DayCycle=true;Sun.Time=23.9f;Sun.DayDuration=1;const auto Revision=Sun.Revision;AdvanceSun(Sun,60);Require(Sun.AutoPosition && Sun.Time<1 && Sun.Revision>Revision,"day cycle failed");
        Sun.DayCycle=false;Sun.AutoPosition=true;auto Position=SolveSun(Sun,12);
        Require(std::fabs(Position.Azimuth-97.6162895)<.01 && std::fabs(Position.Elevation-74.1076284)<.01,"solar solver differs from reference");
        Click(145,135);Require(Sun.Tab==1,"sun tab navigation failed");
        const float OldTime=Sun.Time;Drag(70,370,290,375);Require(Sun.AutoPosition && std::fabs(Sun.Time-OldTime)>1,"solar day graph did not update time");
        Click(323,135);Require(Sun.Tab==3,"sun disc tab failed"); Click(190,782);Require(OpenFlare,"lens flare navigation callback failed");
        for(Panel=1;Panel<=3;Panel++) for(int I=0;I<4;I++) {
            if(Panel==1)Cloud.Tab=I;else if(Panel==2)Atmo.Tab=I;else Flare.Tab=I;
            Render();Render();Save(Output/((Panel==1?"cloud-":Panel==2?"atmosphere-":"flare-")+std::to_string(I)+".png"));
        }
        // Capture the actual scrollable window at the end of every tab, using pristine settings.
        const auto SavedSun=Sun;const auto SavedCloud=Cloud;const auto SavedAtmo=Atmo;const auto SavedFlare=Flare;
        Sun=SolarSettings{};Cloud=CloudSettings{};Atmo=AtmosphereSettings{};Flare=FlareSettings{};
        for(Panel=0;Panel<4;++Panel) for(int I=0;I<4;++I) {
            if(Panel==0)Sun.Tab=I;else if(Panel==1)Cloud.Tab=I;else if(Panel==2)Atmo.Tab=I;else Flare.Tab=I;
            ScrollRequest=0;Render();Render();
            ScrollRequest=100000.f;Render();Render();
            if(ScrollMax>1.f) {
                Require(ActualScroll>0.f,"inspector failed to scroll");
                Save(Output/(std::string("scroll-")+(Panel==0?"sun-":Panel==1?"cloud-":Panel==2?"atmosphere-":"flare-")+std::to_string(I)+"-end.png"));
            }
        }
        Sun=SavedSun;Cloud=SavedCloud;Atmo=SavedAtmo;Flare=SavedFlare;
        ScrollRequest=0;Render();Render();ScrollRequest=-1;
        Panel=1;Cloud.Tab=0;Render();Render();
        Require(std::fabs(CloudDensity(Cloud,-1000,2000,1800)-.527226235)<.0001,"cloud density differs from reference");
        const auto Atlas=CloudAtlas(Cloud,8,4,640.f/440.f);
        Require((Atlas[0]&0xffffffu)==(77u|(87u<<8)|(99u<<16)),"cloud atlas differs from reference");
        const float Coverage=Cloud.Coverage;Drag(100,700,300,720);Require(std::fabs(Cloud.Coverage-Coverage)>.1f && Cloud.Revision>0,"cloud diagram drag failed");
        Click(145,148);Require(Cloud.Tab==1,"cloud formation tab failed");
        const float Base=Cloud.Base;Drag(80,540,250,540);Require(Cloud.Base!=Base,"cloud base slider failed");
        Click(320,148);Require(Cloud.Tab==3,"cloud weather tab failed");Click(100,251);Require(!Cloud.FollowWind,"wind follow toggle failed");
        Click(190,305);Require(OpenWind,"wind navigation callback failed");Click(100,625);Require(Cloud.LightningEnabled,"lightning toggle failed");
        Panel=2;Atmo.Tab=0;Render();Render();
        const auto Transmission=AtmosphereTransmission(Atmo);
        Require(std::fabs(Transmission[0]-.90730425099)<1e-6 && std::fabs(Transmission[1]-.79754276263)<1e-6 && std::fabs(Transmission[2]-.64877796593)<1e-6,"atmosphere transmission differs from reference");
        const float Altitude=Atmo.ProbeAltitude;
        Drag(180,300,180,190);Require(std::fabs(Atmo.ProbeAltitude-Altitude)>1000 && Atmo.Revision>0,"atmosphere probe failed");
        Click(230,148);Require(Atmo.Tab==2,"atmosphere light tab failed");
        Click(320,148);Require(Atmo.Tab==3,"atmosphere baking tab failed");Click(90,335);Require(!Atmo.UseBakedAtmosphere,"atmosphere baked-image toggle failed");
        Click(230,300);Require(BakeRequested,"atmosphere bake callback failed");
        TestPreviews=true;Render();Require(BakedDraws>0,"baked preview callback failed");Atmo.Tab=0;Render();Require(SkyDraws>0,"sky preview callback failed");TestPreviews=false;
        Panel=3;Flare.Tab=0;Render();Render();
        const auto Radiance=FlareRadiance(Flare,.6f,.5f,640.f/280.f);
        Require(std::fabs(Radiance[0]-.00443976394)<1e-6 && std::fabs(Radiance[1]-.00411146938)<1e-6 && std::fabs(Radiance[2]-.00520002989)<1e-6,"flare optical kernel differs from reference");
        Click(300,182);Require(ExportWidth==640 && ExportHeight==280 && Exported.size()==640*280*3,"flare HDR export callback failed");
        const auto Pfm=EncodeFlarePfm({1.f,2.f,3.f,4.f,5.f,6.f},1,2);
        const std::string PfmHeader="PF\n1 2\n-1.0\n";
        Require(Pfm.size()==PfmHeader.size()+24 && std::equal(PfmHeader.begin(),PfmHeader.end(),Pfm.begin()) && Pfm[PfmHeader.size()+2]==0x80 && Pfm[PfmHeader.size()+3]==0x40,"PFM orientation or encoding failed");
        const float SourceX=Flare.PreviewX;
        Drag(100,350,280,370);Require(std::fabs(Flare.PreviewX-SourceX)>.1f && Flare.Revision>0,"flare source drag failed");
        Click(145,645);Require(Flare.Tab==1,"flare ghosts tab failed");
        // Exercise keyboard focus and arrows through actual ImGui input events.
        Io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        auto Key=[&](ImGuiKey K,bool Shift=false){if(Shift){Io.AddKeyEvent(ImGuiMod_Shift,true);Io.AddKeyEvent(ImGuiKey_LeftShift,true);}Io.AddKeyEvent(K,true);Render();Io.AddKeyEvent(K,false);if(Shift){Io.AddKeyEvent(ImGuiMod_Shift,false);Io.AddKeyEvent(ImGuiKey_LeftShift,false);}Render();};
        auto Top=[&](){ScrollRequest=0;Render();Render();ScrollRequest=-1;};
        Sun=SolarSettings{};Panel=0;Top();Click(190,255);const float Az=Sun.AzimuthDegrees;
        Key(ImGuiKey_RightArrow);Require(std::fabs(Sun.AzimuthDegrees-(Az+1))<.01f,"sun vector keyboard arrow failed");
        Sun.Preset=1;Key(ImGuiKey_Home);Require(Sun.AzimuthDegrees==145 && Sun.ElevationDegrees==38 && Sun.Preset==-1,"sun vector Home/preset failed");
        Key(ImGuiKey_UpArrow,true);Require(Sun.ElevationDegrees==48,"sun vector Shift-arrow failed");
        Sun.Tab=1;Top();Click(160,370);const float Time=Sun.Time;Key(ImGuiKey_RightArrow,true);
        Require(std::fabs(Sun.Time-Time-2.5f)<.01f,"sun day graph Shift-arrow failed");Key(ImGuiKey_Home);Require(Sun.Time==12,"sun day graph Home failed");
        Cloud=CloudSettings{};Panel=1;Top();Click(120,700);const float Cov=Cloud.Coverage;Key(ImGuiKey_RightArrow);
        Require(std::fabs(Cloud.Coverage-Cov-.01f)<.001f,"cloud mix keyboard arrow failed");Key(ImGuiKey_Home);Require(Cloud.Coverage==.55f && Cloud.Density==1.f,"cloud mix Home failed");
        Atmo=AtmosphereSettings{};Panel=2;Top();Click(130,300);Key(ImGuiKey_Home);Require(Atmo.ProbeAltitude==0,"atmosphere probe Home failed");
        Key(ImGuiKey_RightArrow,true);Require(Atmo.ProbeAltitude==5000,"atmosphere probe Shift-arrow failed");
        Atmo.Tab=1;Top();Click(100,350);Key(ImGuiKey_RightArrow);Key(ImGuiKey_Home);Require(Atmo.SelectedScaleHeight==1 && Atmo.MieHeight==1200,"atmosphere height selection/Home failed");
        Key(ImGuiKey_UpArrow,true);Require(Atmo.MieHeight==2200,"atmosphere height Shift-arrow failed");
        Flare=FlareSettings{};Panel=3;Top();Click(42,312);Require(std::fabs(Flare.PreviewX-.06f)<.001f && std::fabs(Flare.PreviewY-.08f)<.001f,"flare source minimum clamp failed");
        Key(ImGuiKey_Home);Require(Flare.PreviewX==.28f && Flare.PreviewY==.43f,"flare source Home failed");
        Key(ImGuiKey_UpArrow,true);Require(std::fabs(Flare.PreviewY-.23f)<.001f,"flare source Shift-arrow failed");
        Click(338,440);Require(std::fabs(Flare.PreviewX-.94f)<.001f && std::fabs(Flare.PreviewY-.92f)<.001f,"flare source maximum clamp failed");
        Cloud=CloudSettings{};Panel=1;Cloud.Tab=1;Top();Click(100,540);Key(ImGuiKey_Home);Require(Cloud.Base==100,"cloud slider keyboard Home failed");
        Key(ImGuiKey_RightArrow,true);Require(Cloud.Base==1100,"cloud slider keyboard Shift-arrow failed");
        ScrollRequest=100000;Render();Render();ScrollRequest=-1;Click(70,495);Require(Cloud.LayerStackOpen,"cloud layer stack drawer failed");
        Click(120,720);Key(ImGuiKey_Home);Require(Cloud.Thickness==100,"cloud layer stack keyboard Home failed");
        Key(ImGuiKey_UpArrow,true);Require(Cloud.Thickness==1100,"cloud layer stack keyboard Shift-arrow failed");
        Atmo=AtmosphereSettings{};Panel=2;Atmo.Tab=2;Top();ScrollRequest=100000;Render();Render();ScrollRequest=-1;
        Click(70,720);Require(Atmo.HorizonShapingOpen,"atmosphere horizon drawer failed");
        Flare=FlareSettings{};Panel=3;Flare.Tab=1;Top();ScrollRequest=100000;Render();Render();ScrollRequest=-1;
        Click(120,370);Key(ImGuiKey_Home);Require(Flare.GhostSpacing==1 && Flare.Preset==4,"flare ghost diagram keyboard Home failed");
        Key(ImGuiKey_RightArrow,true);Require(std::fabs(Flare.GhostSpacing-1.5f)<.001f,"flare ghost diagram Shift-arrow failed");
        Flare.Tab=2;Top();ScrollRequest=100000;Render();Render();ScrollRequest=-1;Click(160,552);Key(ImGuiKey_Home);
        Require(std::fabs(Flare.HaloRadius-.22f)<.001f && Flare.Preset==4,"flare halo diagram Home failed");Key(ImGuiKey_UpArrow,true);Require(std::fabs(Flare.HaloRadius-.32f)<.001f,"flare halo diagram Shift-arrow failed");
        std::cout<<"Experimental environment captures and interaction checks passed\n";
        ImGui::DestroyContext();return 0;
    } catch(const std::exception& E) {std::cerr<<E.what()<<'\n';return 1;}
}
