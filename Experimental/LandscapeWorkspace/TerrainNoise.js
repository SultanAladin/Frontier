/*==========================================================================================================================================
                                                             TERRAINNOISE.JS
==========================================================================================================================================*/
// 🧩 Seeded volumetric lattice noise and multiplicative octave terrain fields, matched between CPU and GLSL.

export const TerrainCategories = {fractal:6,multifractal:7,'ridged-multifractal':8,badlands:9,'ridge-noise':10,'fractal-ridges':11,'sand-dunes':12};

function Lattice(X,Y,Z,Seed)
{
    let Bits=(Math.imul(X,374761393)^Math.imul(Y,668265263)^Math.imul(Z,2147483647)^Seed)>>>0;
    Bits=Math.imul(Bits^(Bits>>>13),1274126177)>>>0;
    return ((Bits^(Bits>>>16))>>>0)/4294967295;
}

export function LatticeNoise(X,Y,Z,Seed)
{
    const IX=Math.floor(X),IY=Math.floor(Y),IZ=Math.floor(Z);
    const Smooth=Coordinate=>Coordinate*Coordinate*(3-2*Coordinate);
    const U=Smooth(X-IX),V=Smooth(Y-IY),W=Smooth(Z-IZ);
    let Sum=0;
    for(let DZ=0;DZ<2;DZ++)for(let DY=0;DY<2;DY++)for(let DX=0;DX<2;DX++)
        Sum+=Lattice(IX+DX,IY+DY,IZ+DZ,Seed)*(DX?U:1-U)*(DY?V:1-V)*(DZ?W:1-W);
    return Sum;
}

export function TerrainNoise(X,Y,Z,Category,Seed,Roughness)
{
    let Sum=0,Weight=1,Normalization=0,Product=1;
    const Persistence=.35+Roughness*.3;
    for(let Octave=0;Octave<4;Octave++)
    {
        const Noise=LatticeNoise(X,Y,Z,(Seed+Octave*1013)>>>0),Ridge=1-Math.abs(Noise*2-1);
        let Contribution=Noise;
        if(Category===8||Category===9||Category===10||Category===11)Contribution=Ridge*Ridge;
        if(Category===9)Contribution=Contribution*Contribution;
        if(Category===10)return Ridge;
        Sum+=Contribution*Weight;Normalization+=Weight;
        Product*=1+(Contribution-.5)*Weight;
        X=X*2.03+17.1;Y=Y*2.03-9.2;Z=Z*2.03+5.7;Weight*=Persistence;
    }
    if(Category===7||Category===8)return Math.max(0,Math.min(1,(Product-.25)/2.25));
    if(Category===12)
    {
        const Warp=LatticeNoise(X*.03,Y*.03,Z*.03,Seed);
        return .5+.5*Math.sin(X*.12+Warp*2+Y*.035)*(.65+.35*Sum/Normalization);
    }
    return Sum/Normalization;
}

export function TerrainGradientBound(Category,Roughness)
{
    const Persistence=.35+Roughness*.3;
    let Weight=1,Frequency=1,Normalization=0,Gradient=0,ProductMaximum=1;
    for(let Octave=0;Octave<4;Octave++)
    {
        Normalization+=Weight;Gradient+=Weight*Frequency*2.6;ProductMaximum*=1+.5*Weight;
        Weight*=Persistence;Frequency*=2.03;
    }
    if(Category===7||Category===8)return Gradient*ProductMaximum/2.25*(Category===8?4:1);
    if(Category===10)return 5.2;
    if(Category===12)return 6;
    return Gradient/Normalization*(Category===9?8:Category===11?4:1);
}

export const TerrainNoiseShader = `
float Lattice(ivec3 P,uint Seed){uint B=uint(P.x)*374761393u^uint(P.y)*668265263u^uint(P.z)*2147483647u^Seed;B=(B^(B>>13u))*1274126177u;return float(B^(B>>16u))/4294967295.;}
float LatticeNoise(vec3 P,uint Seed){ivec3 I=ivec3(floor(P));vec3 U=fract(P);U=U*U*(3.-2.*U);float S=0.;for(int Z=0;Z<2;Z++)for(int Y=0;Y<2;Y++)for(int X=0;X<2;X++)S+=Lattice(I+ivec3(X,Y,Z),Seed)*(X==1?U.x:1.-U.x)*(Y==1?U.y:1.-U.y)*(Z==1?U.z:1.-U.z);return S;}
float TerrainNoise(vec3 P,int Category,uint Seed,float Roughness){float Sum=0.,Weight=1.,Normalization=0.,Product=1.;float Persistence=.35+Roughness*.3;for(int O=0;O<4;O++){float N=LatticeNoise(P,Seed+uint(O)*1013u),R=1.-abs(N*2.-1.);float C=N;if(Category==8||Category==9||Category==10||Category==11)C=R*R;if(Category==9)C*=C;if(Category==10)return R;Sum+=C*Weight;Normalization+=Weight;Product*=1.+(C-.5)*Weight;P=P*2.03+vec3(17.1,-9.2,5.7);Weight*=Persistence;}if(Category==7||Category==8)return clamp((Product-.25)/2.25,0.,1.);if(Category==12){float Warp=LatticeNoise(P*.03,Seed);return .5+.5*sin(P.x*.12+Warp*2.+P.y*.035)*(.65+.35*Sum/Normalization);}return Sum/Normalization;}
`;
