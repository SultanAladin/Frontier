/*==========================================================================================================================================
                                                    CLOUDDIAGNOSTIC.CPP
==========================================================================================================================================*/
// 🧩 Static top-down density diagnostic; this is not lit sky rendering.
#include "Experimental/EnvironmentInspectors/CloudDiagnostic.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace Frontier::Experimental
{
namespace
{
float Clamp(float V) { return std::clamp(V, 0.f, 1.f); }
using Field = std::array<uint8_t,64*64*64>;
Field MakeField()
{
    uint32_t Seed=0x31a59e27u;
    auto Random=[&]() { Seed^=Seed<<13; Seed^=Seed>>17; Seed^=Seed<<5; return double(Seed)/4294967296.0; };
    std::array<std::vector<float>,3> Grids;
    for(int O=0;O<3;++O) { const int N=8<<O; Grids[O].resize(N*N*N); for(auto& V:Grids[O]) V=Random(); }
    Field Out{};
    for(int Z=0;Z<64;++Z) for(int Y=0;Y<64;++Y) for(int X=0;X<64;++X)
    {
        double V=0, Weight=0;
        for(int O=0;O<3;++O)
        {
            const int N=8<<O; const double Scale=double(N)/64, PX=X*Scale, PY=Y*Scale, PZ=Z*Scale;
            const int IX=int(std::floor(PX)), IY=int(std::floor(PY)), IZ=int(std::floor(PZ));
            auto Ease=[](double T){return T*T*(3-2*T);};
            const double FX=Ease(PX-IX), FY=Ease(PY-IY), FZ=Ease(PZ-IZ); double Sum=0;
            for(int K=0;K<2;++K)for(int J=0;J<2;++J)for(int I=0;I<2;++I)
                Sum+=Grids[O][(((IZ+K)%N)*N+(IY+J)%N)*N+(IX+I)%N]*(I?FX:1-FX)*(J?FY:1-FY)*(K?FZ:1-FZ);
            const double W=1.0/(1<<O); V+=Sum*W; Weight+=W;
        }
        Out[(Z*64+Y)*64+X]=static_cast<uint8_t>(std::round(V/Weight*255));
    }
    return Out;
}
float Sample(float X,float Y,float Z)
{
    static const Field Data=MakeField();
    const float Q[]={X*64-.5f,Y*64-.5f,Z*64-.5f};
    int B[3];float F[3];for(int I=0;I<3;++I){B[I]=int(std::floor(Q[I]));F[I]=Q[I]-B[I];}
    auto Wrap=[](int V){return (V%64+64)%64;}; float Result=0;
    for(int K=0;K<2;++K)for(int J=0;J<2;++J)for(int I=0;I<2;++I)
        Result+=Data[(Wrap(B[2]+K)*64+Wrap(B[1]+J))*64+Wrap(B[0]+I)]/255.f*(I?F[0]:1-F[0])*(J?F[1]:1-F[1])*(K?F[2]:1-F[2]);
    return Result;
}
}
float CloudProfile(float H,int Type,float Anvil)
{
    if(H<=0||H>=1)return 0;
    float V=Clamp(H*8)*Clamp((1-H)*3);
    if(Type<2) V=Clamp(H*12)*Clamp((1-H)*8);
    if(Type==3) V*=1+Anvil*H*.5f;
    if(Type>3) V*=std::max(0.f,1-(H*2-1)*(H*2-1));
    return V;
}
float CloudDensity(const CloudSettings& S,float X,float Y,float Z)
{
    if(!S.Enabled)return 0;
    const float Top=std::min(S.Ceiling,S.Base+S.Thickness), H=(Z-S.Base)/std::max(1.f,Top-S.Base);
    if(Top<=S.Base||H<=0||H>=1)return 0;
    const float Scale=std::max(7200.f*S.Scale,1.f); X/=Scale;Y/=Scale;Z/=Scale;
    const float Shape=Sample(X,Y,Z)-S.Detail*(1-Sample(X*4.1f+.21f,Y*4.1f+.47f,Z*4.1f+.13f))*.22f;
    const float E=Clamp((Shape-(1-S.Coverage))/std::max(.08f,S.Coverage*.65f));
    return E*E*(3-2*E)*CloudProfile(H,S.Type,S.Anvil)*S.Density;
}
std::vector<uint32_t> CloudAtlas(const CloudSettings& S,int Width,int Height,float Aspect)
{
    std::vector<uint32_t> Pixels(Width*Height);
    const float Top=std::min(S.Ceiling,S.Base+S.Thickness);
    for(int Y=0;Y<Height;++Y)for(int X=0;X<Width;++X)
    {
        const float U=(X+.5f)/Width,V=(Y+.5f)/Height, PX=(U-.5f)*6000, PY=(.5f-V)*6000/Aspect;
        float Sum=0; for(int K=0;K<8;++K) Sum+=CloudDensity(S,PX,PY,S.Base+(K+.5f)/8*(Top-S.Base));
        const float F=1-std::exp(-std::max(0.f,Sum/8)*3);
        const uint32_t R=uint32_t(std::lround(26+F*180)),G=uint32_t(std::lround(35+F*181)),B=uint32_t(std::lround(47+F*182));
        Pixels[Y*Width+X]=R|(G<<8)|(B<<16)|0xff000000u;
    }
    return Pixels;
}
}
