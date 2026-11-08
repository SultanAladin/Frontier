/*==========================================================================================================================================
                                                            LANDFORMSHADER.JS
==========================================================================================================================================*/
// 🧩 GPU equivalents of region CSG and conservative signed landform primitives.

import { TerrainInfluenceShader } from './TerrainInfluence.js';
import { TerrainNoiseShader } from './TerrainNoise.js';

export const LandformShader = `
${TerrainNoiseShader}
${TerrainInfluenceShader}
uniform sampler2D LandformPrimitives;
uniform sampler2D RegionProfiles;
float SmoothUnion(float A,float B,float K){if(K<=0.)return min(A,B);float H=max(K-abs(A-B),0.)/K;return min(A,B)-H*H*K*.25;}
float SmoothIntersection(float A,float B,float K){return -SmoothUnion(-A,-B,K);}
float CombineDistance(float D,float F,int Mode,float K){if(Mode==1)return SmoothIntersection(D,-F,K);if(Mode==2)return SmoothIntersection(D,F,K);if(Mode==3)return (D+F)*.5;return SmoothUnion(D,F,K);}
float PrimitiveDistance(vec3 P,int Index){
    vec4 A=texelFetch(LandformPrimitives,ivec2(Index,0),0);
    vec4 B=texelFetch(LandformPrimitives,ivec2(Index,1),0);
    vec4 C=texelFetch(LandformPrimitives,ivec2(Index,2),0);
    vec3 Q=P-A.xyz;Q.xz=vec2(Q.x*C.x+Q.z*C.y,-Q.x*C.y+Q.z*C.x);
    vec4 E=texelFetch(LandformPrimitives,ivec2(Index,3),0);
    int Category=int(A.w);
    if(Category>=26){vec4 Parameters=texelFetch(LandformPrimitives,ivec2(Index,4),0);return (Q.y-B.y*TerrainProfile(Q,B.w,Parameters.x,Category,uint(E.z),E.y))/Parameters.y;}
    if(Category>=6){float Persistence=.35+E.y*.3,Weight=1.,F=1.,Normalization=0.,Gradient=0.,ProductMaximum=1.;for(int O=0;O<4;O++){Normalization+=Weight;Gradient+=Weight*F*2.6;ProductMaximum*=1.+.5*Weight;Weight*=Persistence;F*=2.03;}float G=Gradient/Normalization;if(Category==7||Category==8)G=Gradient*ProductMaximum/2.25*(Category==8?4.:1.);else if(Category==10)G=5.2;else if(Category==12)G=6.;else if(Category==9)G*=8.;else if(Category==11)G*=4.;float N=TerrainNoise(Q*vec3(B.w,B.w*.2,B.w),Category,uint(E.z),E.y);return max((Q.y-B.y*(.15+.85*N))/(1.+B.y*.85*B.w*G),Q.y-B.y);}
    float Frequency=2./min(B.x,min(B.y,B.z));
    float First=sin(Q.x*Frequency+E.z)*sin(Q.z*Frequency-E.z)*sin(Q.y*Frequency*.7+E.z);
    float Second=sin(Q.x*Frequency*2.-E.z)*sin(Q.z*Frequency*2.+E.z)*sin(Q.y*Frequency*1.4);
    Q.y+=E.y*(First+Second*.35);
    float Bound=1.+E.y*Frequency*sqrt(2.49)*1.7;
    if(Category==0)return (length(Q/B.xyz)-1.)*min(B.x,min(B.y,B.z))/Bound;
    if(Category==1){vec3 R=abs(Q)-B.xyz;return (length(max(R,0.))+min(max(R.x,max(R.y,R.z)),0.)-B.w)/Bound;}
    if(Category==2){float Radial=length(Q.xz/B.xz);float Cone=(Radial+Q.y/B.y-1.)/length(vec2(1./min(B.x,B.z),1./B.y));return max(Cone,-Q.y-B.y)/Bound;}
    if(Category==5){float SX=B.y/B.x;float SZ=B.y/B.z;return max(max((abs(Q.x)*SX+Q.y-B.y)/length(vec2(SX,1.)),(abs(Q.z)*SZ+Q.y-B.y)/length(vec2(SZ,1.))),-Q.y-B.y)/Bound;}
    if(Category==3)return (length(vec2(length(Q.xy)-B.x,Q.z))-B.z)/Bound;
    return (length(Q-vec3(clamp(Q.x,-B.x,B.x),0,0))-B.y)/Bound;
}
float RegionStack(vec3 P,float D,vec4 Profile,vec4 Parameters,float Top,float Planar,out float Residual){
    float Feature=1e8,Envelope=Parameters.w;if(Parameters.x>.5)D=P.y-Top;
    for(int Offset=0;Offset<512;Offset++){
        if(Offset>=int(Profile.y))break;
        int Index=int(Profile.x)+Offset;
        vec4 A=texelFetch(LandformPrimitives,ivec2(Index,0),0);
        vec4 B=texelFetch(LandformPrimitives,ivec2(Index,1),0);
        vec4 C=texelFetch(LandformPrimitives,ivec2(Index,2),0);
        vec4 E=texelFetch(LandformPrimitives,ivec2(Index,3),0);
        if(int(A.w)>=26){float W=RadialWeight(P,A,B,E)*CoastWeight(Planar,Profile.z);float Limit=B.y+C.w*.25;if(W>0.){vec3 Q=P-A.xyz;Q.xz=vec2(Q.x*C.x+Q.z*C.y,-Q.x*C.y+Q.z*C.x);float H=B.y*TerrainProfile(Q,B.w,texelFetch(LandformPrimitives,ivec2(Index,4),0).x,int(A.w),uint(E.z),E.y);float F=P.y-A.y+(int(C.z)==1?H:-H);float Candidate=int(C.z)==1||int(C.z)==2?SmoothIntersection(D,F,C.w):int(C.z)==3?(D+F)*.5:SmoothUnion(D,F,C.w);D+=W*clamp(Candidate-D,-Limit,Limit);Envelope+=Limit*W;}continue;}
        Feature=min(Feature,PrimitiveDistance(P,Index));
        if(E.x>.5){D=CombineDistance(D,Feature,int(C.z),C.w);Feature=1e8;}
    }
    Residual=D;return Parameters.x>.5?max(D/Parameters.y,(P.y-Envelope)/Parameters.z):D;
}
`;
