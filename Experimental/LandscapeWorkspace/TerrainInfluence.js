/*==========================================================================================================================================
                                                           TERRAININFLUENCE.JS
==========================================================================================================================================*/
// 🧩 Continuous volumetric terrain profiles with compact radial support and conservative field-gradient bounds.

import { TerrainNoise,TerrainGradientBound } from './TerrainNoise.js';
export const TerrainInfluenceDefaults={Influence:1,InfluenceRadius:1.15,InfluenceFalloff:1,InfluenceX:0,InfluenceZ:0};
export const TerrainGeneratorCodes={fractal:26,multifractal:27,'ridged-multifractal':28,badlands:29,'ridge-noise':30,'fractal-ridges':31,'sand-dunes':32,ridges:31,dunes:32,mesa:33,fault:34,craters:35};
export function RadialWeight(Primitive,X,Z)
{
    const Radius=Primitive.Radii[0],Feather=Radius*Primitive.Radii[2];
    const T=Math.max(0,Math.min(1,(Radius-Math.hypot(X-Primitive.Centre[0],Z-Primitive.Centre[2]))/Feather));
    return Primitive.Influence*T*T*(3-2*T);
}
export function CoastWeight(PlanarDistance, Width)
{
    if (Width <= 0) return PlanarDistance <= 0 ? 1 : 0;
    const Fraction = Math.max(0, Math.min(1, -PlanarDistance / Width));
    return Fraction * Fraction * (3 - 2 * Fraction);
}

export function TerrainProfileGradient(Category,Roughness)
{
    const Code=Category-20;
    return Code===13?3*TerrainGradientBound(6,Roughness):Code===14?TerrainGradientBound(6,Roughness)+1:Code===15?8:TerrainGradientBound(Code,Roughness);
}
export function TerrainProfile(Primitive,X,Y,Z)
{
    const ΔX=X-Primitive.Centre[0],ΔZ=Z-Primitive.Centre[2],Frequency=Primitive.Bevel;
    const QX=(ΔX*Primitive.Cosine+ΔZ*Primitive.Sine)*Frequency,QY=(Y-Primitive.Centre[1])*Primitive.VerticalFrequency,QZ=(-ΔX*Primitive.Sine+ΔZ*Primitive.Cosine)*Frequency;
    const Code=Primitive.Category-20;
    if(Code===15)
    {
        const Fract=Coordinate=>Coordinate-Math.floor(Coordinate);
        const DX=Fract(QX+(Primitive.Phase%997)*.013)-.5,DZ=Fract(QZ+(Primitive.Phase%991)*.017)-.5;
        const T=Math.max(0,1-(DX*DX+DZ*DZ+QY*QY*.01)/.16);return T*T;
    }
    const N=TerrainNoise(QX,QY,QZ,Code>12?6:Code,Primitive.Phase,Primitive.Detail);
    if(Code===13){const T=Math.max(0,Math.min(1,(N-.25)*2));return T*T*(3-2*T);}
    if(Code===14)return .5+.3*Math.sin(QX*.5)+.2*N;
    return N;
}
export const TerrainInfluenceShader=`
float RadialWeight(vec3 P,vec4 A,vec4 B,vec4 E){float T=clamp((B.x-length(P.xz-A.xz))/(B.x*B.z),0.,1.);return E.w*T*T*(3.-2.*T);}
float CoastWeight(float Planar, float Width)
{
    if (Width <= 0.) return Planar <= 0. ? 1. : 0.;
    float Fraction = clamp(-Planar / Width, 0., 1.);
    return Fraction * Fraction * (3. - 2. * Fraction);
}
float TerrainProfile(vec3 Q,float Frequency,float VerticalFrequency,int Category,uint Seed,float Roughness){Q*=vec3(Frequency,VerticalFrequency,Frequency);int Code=Category-20;if(Code==15){vec2 R=fract(Q.xz+vec2(float(Seed%997u)*.013,float(Seed%991u)*.017))-.5;float T=max(0.,1.-(dot(R,R)+Q.y*Q.y*.01)/.16);return T*T;}float N=TerrainNoise(Q,Code>12?6:Code,Seed,Roughness);if(Code==13){float T=clamp((N-.25)*2.,0.,1.);return T*T*(3.-2.*T);}if(Code==14)return .5+.3*sin(Q.x*.5)+.2*N;return N;}
`;
