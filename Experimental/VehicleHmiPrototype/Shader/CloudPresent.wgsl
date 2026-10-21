//! include "CloudRecords.wgsl"
@group(0) @binding(6) var CloudColour:texture_2d<f32>;
@group(0) @binding(7) var CloudDepth:texture_2d<f32>;
@group(0) @binding(8) var CloudSceneDepth:texture_depth_2d;
@group(0) @binding(9) var CloudCabin:texture_2d<f32>;
@group(0) @binding(10) var<uniform> CloudControl:CloudParameters;
// Guard foregrounds at full pixel depth when reconstructing fog-only targets.
fn FogBeforeSurface(Pixel:vec2f,Depth:f32)->bool{
 let C=CloudControl;let O=Frame.CameraPosition;let World=ReconstructWorldPosition(Pixel,max(Depth,1e-6));let D=normalize(World-O);let Limit=select(C.Render.z,length(World-O),Depth>0);
 let A=(O-C.FogCentre.xyz)/C.FogSize.xyz;let B=D/C.FogSize.xyz;
 if(C.FogSize.w==1.0||C.FogSize.w==3.0){let Safe=select(vec3f(1e-7),B,abs(B)>vec3f(1e-7));let T0=(-vec3f(1)-A)/Safe;let T1=(vec3f(1)-A)/Safe;let Near=min(T0,T1);let Far=max(T0,T1);return min(Limit,min(Far.x,min(Far.y,Far.z)))>max(0.0,max(Near.x,max(Near.y,Near.z)));}
 let BB=dot(B,B);let AB=dot(A,B);let H=AB*AB-BB*(dot(A,A)-1);if(H<=0){return false;}return min(Limit,(-AB+sqrt(H))/BB)>max(0.0,(-AB-sqrt(H))/BB);
}
// Cloud guards use the full-resolution ray too: low-resolution neighbours
// must not paint cloud colour over geometry in front of every cloud volume.
fn CloudsBeforeSurface(Pixel:vec2f,Depth:f32)->bool{
 let C=CloudControl;let O=Frame.CameraPosition;let World=ReconstructWorldPosition(Pixel,max(Depth,1e-6));let D=normalize(World-O);let Limit=select(C.Render.z,length(World-O),Depth>0);
 if(C.Control.x>.5){let Top=min(C.Control.w,C.Layer.x+C.Layer.y);if(Top>C.Layer.x){
  if(abs(D.z)<1e-7){if(O.z>C.Layer.x&&O.z<Top){return true;}}
  else{let A=(C.Layer.x-O.z)/D.z;let B=(Top-O.z)/D.z;if(min(Limit,max(A,B))>max(0.0,min(A,B))){return true;}}
 }}
 if(C.Centre.w>.5){let A=(O-C.Centre.xyz)/C.Size.xyz;let B=D/C.Size.xyz;
  if(C.LocalShape.x>.5){let Safe=select(vec3f(1e-7),B,abs(B)>vec3f(1e-7));let T0=(-vec3f(1)-A)/Safe;let T1=(vec3f(1)-A)/Safe;let Near=min(T0,T1);let Far=max(T0,T1);if(min(Limit,min(Far.x,min(Far.y,Far.z)))>max(0.0,max(Near.x,max(Near.y,Near.z)))){return true;}}
  else{let BB=dot(B,B);let AB=dot(A,B);let H=AB*AB-BB*(dot(A,A)-1);if(H>0){if(min(Limit,(-AB+sqrt(H))/BB)>max(0.0,(-AB-sqrt(H))/BB)){return true;}}}
 }
 return false;
}
fn AddClouds(Pixel:vec2f,Original:vec3f)->vec3f{
 if(CloudControl.Render.y<.5||Frame.InspectionMode!=0u){return Original;}
 let P=vec2i(Pixel);let Glass=textureLoad(CloudCabin,P,0).r;if(Glass<=0){return Original;}
 let D=textureLoad(CloudSceneDepth,P,0);
 if(!CloudsBeforeSurface(Pixel,D)&&(CloudControl.FogCentre.w<.5||!FogBeforeSurface(Pixel,D))){return Original;}let N=vec2i(textureDimensions(CloudColour));let UV=Pixel*Frame.InverseScreenExtent;
 if(D<=0){let Q=clamp(vec2i(UV*vec2f(N)),vec2i(0),N-1);if(textureLoad(CloudDepth,Q,0).x==0){let S=textureSampleLevel(CloudColour,FlareSampler,UV,0);return mix(Original,Original*S.a+S.rgb,Glass);}}
 let World=ReconstructWorldPosition(Pixel,max(D,1e-6));let Distance=select(0.0,min(60000.0,length(World-Frame.CameraPosition)),D>0);
 let F=UV*vec2f(N)-.5;let B=vec2i(floor(F));let Frac=fract(F);var Sum=0.0;var S=vec4f(0);
 for(var Y=0;Y<2;Y++){for(var X=0;X<2;X++){
  let Q=clamp(B+vec2i(X,Y),vec2i(0),N-1);let Guide=textureLoad(CloudDepth,Q,0).x;
  // Continuous bilateral weighting: the previous hard 2% depth rejection
  // returned Original at entire contour bands on sloping road surfaces.
  if((Guide==0&&Distance==0)||(Guide>0&&Distance>0)){
   let Spatial=select(1.0-Frac.x,Frac.x,X==1)*select(1.0-Frac.y,Frac.y,Y==1);
   let Error=abs(Guide-Distance)/max(.5,Distance*.03);
   let W=Spatial/max(1.0,1.0+Error*Error*Error*Error);
   S+=textureLoad(CloudColour,Q,0)*W;Sum+=W;
  }
 }}
 if(Sum<=1e-12){return Original;}S/=Sum;return mix(Original,Original*S.a+S.rgb,Glass);
}
