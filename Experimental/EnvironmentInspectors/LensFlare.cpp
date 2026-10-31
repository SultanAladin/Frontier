/*==========================================================================================================================================
                                                        LENSFLARE.CPP
==========================================================================================================================================*/
// 🧩 CPU port of the VehicleHMI native optical model; output is linear HDR RGB.
#include "Experimental/EnvironmentInspectors/EnvironmentInspectors.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <limits>
namespace Frontier::Experimental
{
namespace
{
constexpr double Pi=3.14159265358979323846;
double Clamp(double V,double A,double B){return std::clamp(V,A,B);}
double Fract(double V){return V-std::floor(V);}
double Smooth(double A,double B,double V){const double T=Clamp((V-A)/(B-A),0,1);return T*T*(3-2*T);}
double Hue(double H,double S){return Clamp(std::abs(Fract(H+S)*6-3)-1,0,1);}
}
std::array<float,3> FlareRadiance(const FlareSettings& S,float U,float V,float Aspect)
{
    if(S.Intensity<=0)return {0,0,0};
    const double Edge=Smooth(-.15,.05,S.PreviewX)*(1-Smooth(.95,1.15,S.PreviewX))*Smooth(-.15,.05,S.PreviewY)*(1-Smooth(.95,1.15,S.PreviewY));
    const double X=(U-.5)*Aspect,Y=V-.5,SX=(S.PreviewX-.5)*Aspect,SY=S.PreviewY-.5,DX=X-SX,DY=Y-SY;
    const double Spread=Clamp(S.Spread,.25,2),D=std::hypot(DX,DY),A=S.Rotation*Pi/180,RX=DX*std::cos(A)+DY*std::sin(A),RY=-DX*std::sin(A)+DY*std::cos(A),C=S.Chromatic;
    const double Cool=std::exp(-std::abs(DY)*95/Spread)*std::exp(-std::abs(DX)*2.2/Spread)*.55*(S.Anamorphic?1:0)*S.Streak;
    const double Warm=std::exp(-std::abs(RY)*160/Spread)*std::exp(-std::abs(RX)*3/Spread)*.65*(S.WarmStreak?1:0)*S.Streak;
    const double Angle=D>1e-8?std::atan2(RY,RX):0,Pairs=std::floor(Clamp(S.Preset==4?S.Rays:S.Aperture,2,12)+.5);
    const double Burst=std::pow(std::abs(std::sin(Angle*Pairs)),32)*std::exp(-D*5/Spread)*.6*(S.Burst?1:0);
    const double Core=(S.Anamorphic||S.WarmStreak||S.Burst)?std::exp(-D*80/Spread)*.8+std::exp(-D*9/Spread)*.035:0;
    double RGB[]={Cool*(1-.55*C)+Warm+Burst+Core,Cool*(1-.35*C)+Warm*.8+Burst*.95+Core*.9,Cool+Warm*.55+Burst*.82+Core*.75};
    constexpr double HueOffset[]={0,.6666667,.3333333};
    const int Count=int(std::round(Clamp(S.GhostCount,0,24)));
    for(int I=0;I<Count;++I)
    {
        const double K=1-(.6+(I+1.)/Count*(1.7+S.GhostSpacing)),GX=X-SX*K,GY=Y-SY*K;double GD=std::hypot(GX,GY);
        if(S.GhostShape>=3){const double Step=Pi*2/S.GhostShape,T=GD>1e-8?std::atan2(GY,GX):0,Fold=T-Step*std::floor((T+Step*.5)/Step);GD*=std::cos(Fold)/std::cos(Pi/S.GhostShape);}
        const double Radius=(.025+.06*Fract(I*.618+.31))*Spread,H=Fract(I*.23+.5);
        for(int J=0;J<3;++J){const double R=Radius*(1+(J-1)*C*.035),Spectral=(Smooth(R,R*.35,GD)*.6+std::exp(-std::abs(GD-R)*350))*.14*S.GhostBrightness;RGB[J]+=Spectral*(1+(Hue(H,HueOffset[J])-1)*C);}
    }
    const double HX=X-SX*.25,HY=Y-SY*.25,HD=std::hypot(HX,HY),Offset=std::abs(HD-S.HaloRadius);
    const double Halo=Smooth(Clamp(S.HaloWidth,.005,.2),0,Offset)*.09*S.HaloBrightness,H=Fract((HD>1e-8?std::atan2(HY,HX):0)/(Pi*2)+Offset*8);
    for(int J=0;J<3;++J) RGB[J]=(RGB[J]+Halo*(1+(Hue(H,HueOffset[J])-1)*C*.8))*S.Intensity*Edge;
    return {float(RGB[0]),float(RGB[1]),float(RGB[2])};
}
std::vector<float> RenderFlareImage(const FlareSettings& S,int W,int H)
{
    if(W<=0||H<=0)return {};
    std::vector<float> Out(size_t(W)*size_t(H)*3);
    for(int Y=0;Y<H;++Y)for(int X=0;X<W;++X){const auto C=FlareRadiance(S,(X+.5f)/W,(Y+.5f)/H,float(W)/H);for(int J=0;J<3;++J)Out[(size_t(Y)*W+X)*3+J]=C[J];}
    return Out;
}
std::vector<uint8_t> EncodeFlarePfm(const std::vector<float>& Pixels,int W,int H)
{
    if(W<=0||H<=0||Pixels.size()!=size_t(W)*size_t(H)*3) return {};
    static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559, "PFM export requires IEEE 754 32-bit floats");
    const std::string Header="PF\n"+std::to_string(W)+" "+std::to_string(H)+"\n-1.0\n";
    std::vector<uint8_t> Out(Header.begin(),Header.end());Out.reserve(Out.size()+Pixels.size()*4);
    // PFM stores rows bottom to top and a negative scale denotes little-endian floats.
    for(int Y=H-1;Y>=0;--Y)for(int X=0;X<W;++X)for(int C=0;C<3;++C){
        uint32_t Bits;const float Value=Pixels[(size_t(Y)*W+X)*3+C];std::memcpy(&Bits,&Value,sizeof Bits);
        for(int B=0;B<4;++B)Out.push_back(static_cast<uint8_t>((Bits>>(8*B))&0xff));
    }
    return Out;
}

}
