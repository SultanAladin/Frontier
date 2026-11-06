/*==========================================================================================================================================
                                                        VOLUMERASTERIZATION.JS
==========================================================================================================================================*/
// 🧩 WebGL2 capsule-field ray marching with procedural ocean shading.

import { CompileRegion, CombineModes, MaximumPrimitives } from './LandformStack.js';
import { TerrainGradientBound } from './TerrainNoise.js';
import { LandformShader } from './LandformShader.js';

export function CreateVolumeRasterization(Canvas)
{
    const GL = Canvas.getContext('webgl2', { antialias: false, preserveDrawingBuffer: true });
    if (!GL) throw Error('WebGL2 is unavailable. Mask editing still works.');
    const Debug = GL.getExtension('WEBGL_debug_renderer_info');
    const Software = Debug && /swiftshader|llvmpipe/i.test(GL.getParameter(Debug.UNMASKED_RENDERER_WEBGL));
    let LastDraw = -1;
    let PendingFence = null;
    let UploadedLayerCount = 0;
    let SubmittedOrbit = null;
    let SubmittedTarget = null;
    let ContainsNoise = false;
    let VolumeRevision = 0, SubmittedRevision = 0;
    const Vertex = `#version 300 es
    in vec2 Position;
    void main(){gl_Position=vec4(Position,0,1);}`;
    const Fragment = `#version 300 es
    precision highp float;
    uniform vec2 Pixels;
    uniform float Time, Yaw, Pitch, Orbit, Wave;
    uniform vec3 Target;
    uniform bool Ocean, Probe, ProbeDistance;
    uniform vec3 ProbePoint;
    uniform sampler2D Capsules;
    uniform int Count, LayerCount, MarchSteps;
    uniform float SurfaceTolerance;
    uniform vec4 Regions[256];
    uniform vec4 Profiles[16];
    uniform vec3 Colors[16];
    out vec4 Pixel;
    ${LandformShader}
    float Capsule(vec2 P,vec2 A,vec2 B,float R){vec2 AB=B-A;return length(P-A-AB*clamp(dot(P-A,AB)/max(dot(AB,AB),0.00001),0.,1.))-R;}
    float Rectangle(vec2 P,vec4 R){vec2 Q=abs(P-(R.xy+R.zw)*.5)-(R.zw-R.xy)*.5;return length(max(Q,0.))+min(max(Q.x,Q.y),0.);}
    vec2 Field(vec3 P){
        float Distances[16];for(int L=0;L<16;L++)Distances[L]=1e7;
        for(int I=0;I<512;I++){
            if(I>=Count)break;
            vec4 A=texelFetch(Capsules,ivec2(I,0),0);vec4 B=texelFetch(Capsules,ivec2(I,1),0);
            int L=int(B.w);float D=Capsule(P.xz,A.xy,A.zw,B.x);
            Distances[L]=B.y>.5?max(Distances[L],-D):min(Distances[L],D);
        }
        vec2 Result=vec2(1e7,0);
        for(int L=0;L<16;L++){
            if(L>=LayerCount)break;
            vec4 Profile=Profiles[L];if(Profile.w<.5)continue;
            float D=1e8;
            for(int R=0;R<16;R++){
                if(R>=int(Profile.y))break;
                int Slot=L*16+R;
                vec4 Bounds=Regions[Slot];
                vec4 Stack=texelFetch(RegionProfiles,ivec2(Slot,0),0);
                float Radius=min(Stack.z,min((Bounds.z-Bounds.x)*.49,(Bounds.w-Bounds.y)*.49));
                float Clip=Rectangle(P.xz,Bounds+vec4(Radius,Radius,-Radius,-Radius))-Radius;
                float H=max(Distances[L],Clip);if(max(H,-P.y-Profile.x)>=min(D,Result.x))continue;float V=abs(P.y+Profile.x*.5)-Profile.x*.5;
                if(Stack.w>0.){vec4 A=texelFetch(LandformPrimitives,ivec2(int(Stack.x),0),0),B=texelFetch(LandformPrimitives,ivec2(int(Stack.x),1),0);float Upper=max((P.y-A.y-B.y*.15)*Stack.w,P.y-A.y-B.y);if(H>Upper+Stack.z){D=min(D,max(H,-P.y-Profile.x));continue;}}
                float RD=length(max(vec2(H,V),0.))+min(max(H,V),0.);
                RD=RegionStack(P,RD,Stack,texelFetch(RegionProfiles,ivec2(Slot,1),0),0.);
                RD=SmoothIntersection(RD,H,Stack.z);
                RD=max(RD,-P.y-Profile.x);
                D=min(D,RD);
            }
            if(Profile.z>.5){vec4 R=Regions[L*16];D=max(D,-(length(P-vec3((R.x+R.z)*.5,0.,(R.y+R.w)*.5))-Profile.x*.65));}
            if(D<Result.x)Result=vec2(D,float(L));
        }return Result;
    }
    float Sea(vec2 P){float H=0.;float Frequency=.012;float Amplitude=1.;for(int I=0;I<9;I++){float Angle=float(I)*2.399963+0.4;vec2 Direction=vec2(cos(Angle),sin(Angle));H+=sin(dot(P,Direction)*Frequency+Time*sqrt(9.81*Frequency)+float(I)*1.731)*Amplitude;Frequency*=1.793;Amplitude*=.52;}return H*Wave;}
    vec3 Sky(vec3 D){vec3 Sun=normalize(vec3(-.5,.65,-.7));return mix(vec3(.64,.73,.76),vec3(.17,.29,.38),pow(max(D.y,0.),.55))+vec3(1.,.81,.56)*pow(max(dot(D,Sun),0.),180.)*2.;}
    void main(){
        if(Probe){float D=Field(ProbePoint).x;if(ProbeDistance){uint Bits=floatBitsToUint(D);Pixel=vec4(uvec4(Bits&255u,(Bits>>8u)&255u,(Bits>>16u)&255u,(Bits>>24u)&255u))/255.;}else Pixel=vec4(D<0.?1.:0.,D>=0.?1.:0.,0.,1.);return;}
        vec2 UV=(gl_FragCoord.xy*2.-Pixels)/Pixels.y;
        vec3 Eye=Target+Orbit*vec3(cos(Pitch)*sin(Yaw),sin(Pitch),cos(Pitch)*cos(Yaw));
        vec3 Forward=normalize(Target-Eye);vec3 Right=normalize(cross(Forward,vec3(0,1,0)));vec3 Up=cross(Right,Forward);
        vec3 Ray=normalize(Forward*1.65+Right*UV.x+Up*UV.y);
        vec3 Color=Sky(Ray);float Travel=0.;vec2 Hit=vec2(1e7,0);bool Found=false;
        for(int I=0;I<1024;I++){if(I>=MarchSteps)break;vec3 P=Eye+Ray*Travel;Hit=Field(P);if(Hit.x<SurfaceTolerance){Found=true;break;}Travel+=max(Hit.x*.85,SurfaceTolerance*.6);if(Travel>Orbit*5.+5000.)break;}
        float Water=1e8;
        if(Ocean&&Ray.y<-.001){Water=-Eye.y/Ray.y;for(int I=0;I<5;I++){vec3 P=Eye+Ray*Water;Water=(Sea(P.xz)-Eye.y)/Ray.y;}if(Water<0.)Water=1e8;}
        if(Found&&Travel<Water){
            vec3 P=Eye+Ray*Travel;vec2 E=vec2(.7,0);vec3 N=normalize(vec3(Field(P+E.xyy).x-Field(P-E.xyy).x,Field(P+E.yxy).x-Field(P-E.yxy).x,Field(P+E.yyx).x-Field(P-E.yyx).x));
            float Light=max(dot(N,normalize(vec3(-.5,.8,-.7))),0.);float Grain=.94+.06*sin(P.x*.37)*sin(P.z*.29)*sin(P.y*.43);
            Color=Colors[int(Hit.y)]*(.35+Light*.7)*Grain;Color=mix(Color,vec3(.57,.53,.39),.35*(1.-smoothstep(0.,7.,P.y)));
            Color=mix(Color,Sky(Ray),1.-exp(-Travel*.00009));
        }else if(Water<1e7){
            vec3 P=Eye+Ray*Water;float E=.8;vec3 N=normalize(vec3(Sea(P.xz-vec2(E,0))-Sea(P.xz+vec2(E,0)),2.*E,Sea(P.xz-vec2(0,E))-Sea(P.xz+vec2(0,E))));
            float Fresnel=.025+.975*pow(1.-max(dot(-Ray,N),0.),5.);vec3 Reflection=Sky(reflect(Ray,N));
            Color=mix(vec3(.025,.17,.19),Reflection,Fresnel*.85+.15);vec3 Sun=normalize(vec3(-.5,.65,-.7));Color+=vec3(1.,.86,.63)*pow(max(dot(reflect(-Sun,N),-Ray),0.),160.)*1.8;
            float Shore=Field(vec3(P.x,0.,P.z)).x;float Foam=(1.-smoothstep(0.,9.,Shore))*(.45+.2*sin(Shore*1.6-Time*1.8+sin(P.x*.07)));
            Color=mix(Color,vec3(.78,.85,.8),clamp(Foam,0.,.7));Color=mix(Color,Sky(Ray),1.-exp(-Water*.000065));
        }
        Pixel=vec4(pow(max(Color,0.),vec3(.9)),1.);
    }`;
    function Compile(Category, Text)
    {
        const Shader = GL.createShader(Category);
        GL.shaderSource(Shader, Text);
        GL.compileShader(Shader);
        if (!GL.getShaderParameter(Shader, GL.COMPILE_STATUS)) throw Error(GL.getShaderInfoLog(Shader));
        return Shader;
    }
    const Program = GL.createProgram();
    GL.attachShader(Program, Compile(GL.VERTEX_SHADER, Vertex));
    GL.attachShader(Program, Compile(GL.FRAGMENT_SHADER, Fragment));
    GL.linkProgram(Program);
    if (!GL.getProgramParameter(Program, GL.LINK_STATUS)) throw Error(GL.getProgramInfoLog(Program));
    GL.useProgram(Program);
    const Buffer = GL.createBuffer();
    GL.bindBuffer(GL.ARRAY_BUFFER, Buffer);
    GL.bufferData(GL.ARRAY_BUFFER, new Float32Array([-1,-1, 3,-1, -1,3]), GL.STATIC_DRAW);
    const Position = GL.getAttribLocation(Program, 'Position');
    GL.enableVertexAttribArray(Position);
    GL.vertexAttribPointer(Position, 2, GL.FLOAT, false, 0, 0);
    const Texture = GL.createTexture();
    GL.bindTexture(GL.TEXTURE_2D, Texture);
    GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER, GL.NEAREST);
    GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_MAG_FILTER, GL.NEAREST);
    const Locate = Name => GL.getUniformLocation(Program, Name);
    const LandformTexture = GL.createTexture();
    const RegionTexture = GL.createTexture();
    function Upload(Layers)
    {
        GL.useProgram(Program);
        UploadedLayerCount=Layers.length;ContainsNoise=false;Canvas.dataset.volumeRevision=String(++VolumeRevision);
        GL.activeTexture(GL.TEXTURE0);
        GL.bindTexture(GL.TEXTURE_2D, Texture);
        const PrimitiveSamples = new Float32Array(MaximumPrimitives*20);
        const RegionSamples = new Float32Array(256*8);
        let PrimitiveCount = 0;
        const Capsules = new Float32Array(512 * 8);
        const Profiles = new Float32Array(64);
        const Regions = new Float32Array(1024);
        const Colors = new Float32Array(48);
        let Count = 0;
        Layers.forEach((Layer, Index) =>
        {
            Profiles.set([Layer.Depth, Layer.Regions.length, Layer.Caves ? 1 : 0, Layer.Visible ? 1 : 0], Index * 4);
            Colors.set(Layer.Color.match(/\w\w/g).map(Channel => parseInt(Channel,16) / 255), Index * 3);
            Layer.Regions.forEach((Region, Offset) =>
            {
                Regions.set([Region.X0,Region.Z0,Region.X1,Region.Z1],(Index*16+Offset)*4);
                const Start = PrimitiveCount;
                const Compilation=CompileRegion(Layer,Region);
                for (const Step of Compilation.Steps)
                {
                    Step.Primitives.forEach((Primitive,Ordinal) =>
                    {
                        if(Primitive.Category>=6)ContainsNoise=true;
                        if (PrimitiveCount >= MaximumPrimitives) throw Error('Landform display capacity exceeded; remove operations or split the document.');
                        PrimitiveSamples.set([...Primitive.Centre,Primitive.Category],PrimitiveCount*4);
                        PrimitiveSamples.set([...Primitive.Radii,Primitive.Bevel],(MaximumPrimitives+PrimitiveCount)*4);
                        PrimitiveSamples.set([Primitive.Cosine,Primitive.Sine,CombineModes.indexOf(Step.Mode),Step.Blend],(MaximumPrimitives*2+PrimitiveCount)*4);
                        PrimitiveSamples.set([Ordinal===Step.Primitives.length-1?1:0,Primitive.Detail,Primitive.Phase,Primitive.Influence||0],(MaximumPrimitives*3+PrimitiveCount)*4);
                        PrimitiveSamples.set([Primitive.VerticalFrequency||0,Primitive.FieldBound||1,0,0],(MaximumPrimitives*4+PrimitiveCount)*4);
                        PrimitiveCount++;
                    });
                }
                const Only=Compilation.Steps.length===1&&Compilation.Steps[0].Mode==='union'&&Compilation.Steps[0].Primitives.length===1?Compilation.Steps[0].Primitives[0]:null;
                const NoiseSlope=Only?.Category>=6&&Only.Category<26?1/(1+Only.Radii[1]*.85*Only.Bevel*TerrainGradientBound(Only.Category,Only.Detail)):0;
                RegionSamples.set([Start,PrimitiveCount-Start,Region.Edge||0,NoiseSlope],(Index*16+Offset)*4);
                RegionSamples.set([Compilation.TerrainEnabled?1:0,Compilation.FieldBound,Compilation.EnvelopeSlope,Compilation.EnvelopeTop],(256+Index*16+Offset)*4);
            });
            for (const Stroke of Layer.Strokes)
            for (let Point = 0; Point < Stroke.Points.length; Point++)
            {
                if (Count >= 512) throw Error('GPU capsule capacity exceeded');
                Capsules.set([...Stroke.Points[Math.max(0, Point - 1)], ...Stroke.Points[Point]], Count * 4);
                Capsules.set([Stroke.Radius, Stroke.Erase ? 1 : 0, 0, Index], 512 * 4 + Count * 4);
                Count++;
            }
        });
        GL.texImage2D(GL.TEXTURE_2D, 0, GL.RGBA32F, 512, 2, 0, GL.RGBA, GL.FLOAT, Capsules);
        GL.uniform1i(Locate('Count'), Count);
        GL.uniform1i(Locate('LayerCount'), Layers.length);
        GL.uniform4fv(Locate('Profiles'), Profiles);
        GL.uniform4fv(Locate('Regions'), Regions);
        GL.uniform3fv(Locate('Colors'), Colors);
        GL.uniform1i(Locate('Capsules'),0);
        for (const [Unit,Texture,Width,Height,Samples,Name] of [[1,LandformTexture,MaximumPrimitives,5,PrimitiveSamples,'LandformPrimitives'],[2,RegionTexture,256,2,RegionSamples,'RegionProfiles']])
        {
            GL.activeTexture(GL.TEXTURE0+Unit);
            GL.bindTexture(GL.TEXTURE_2D,Texture);
            GL.texParameteri(GL.TEXTURE_2D,GL.TEXTURE_MIN_FILTER,GL.NEAREST);
            GL.texParameteri(GL.TEXTURE_2D,GL.TEXTURE_MAG_FILTER,GL.NEAREST);
            GL.texImage2D(GL.TEXTURE_2D,0,GL.RGBA32F,Width,Height,0,GL.RGBA,GL.FLOAT,Samples);
            GL.uniform1i(Locate(Name),Unit);
        }
        GL.activeTexture(GL.TEXTURE0);
        LastDraw = -1;
    }
    function Draw(Camera, Seconds, Wave, Ocean)
    {
        if (PendingFence)
        {
            if (GL.clientWaitSync(PendingFence,0,0) === GL.TIMEOUT_EXPIRED) return;
            if(SubmittedOrbit!==null)Canvas.dataset.completedOrbit=String(SubmittedOrbit);
            if(SubmittedTarget!==null)Canvas.dataset.completedTarget=SubmittedTarget;
            Canvas.dataset.completedRevision=String(SubmittedRevision);
            GL.deleteSync(PendingFence);
            PendingFence = null;
        }
        if (Seconds - LastDraw < (Software ? 1 : 1 / 30)) return;
        LastDraw = Seconds;
        const Ratio = Math.min(devicePixelRatio, 1.25, (Number(Canvas.dataset.previewWidth)||(Software ? (UploadedLayerCount>3?240:360) : 1200)) / Canvas.clientWidth);
        const Width = Math.floor(Canvas.clientWidth * Ratio);
        const Height = Math.floor(Canvas.clientHeight * Ratio);
        if (Canvas.width !== Width || Canvas.height !== Height) { Canvas.width = Width; Canvas.height = Height; }
        GL.viewport(0, 0, Width, Height);
        GL.uniform1i(Locate('MarchSteps'),ContainsNoise?1024:256);GL.uniform1f(Locate('SurfaceTolerance'),.02);
        GL.uniform2f(Locate('Pixels'), Width, Height);
        GL.uniform1f(Locate('Time'), Seconds);
        GL.uniform1f(Locate('Yaw'), Camera.Yaw);
        GL.uniform1f(Locate('Pitch'), Camera.Pitch);
        GL.uniform1f(Locate('Orbit'), Camera.Orbit);
        GL.uniform3fv(Locate('Target'), Camera.Target);
        GL.uniform1f(Locate('Wave'), Wave);
        GL.uniform1i(Locate('Ocean'), Ocean ? 1 : 0);
        GL.drawArrays(GL.TRIANGLES, 0, 3);
        SubmittedOrbit=Camera.Orbit;SubmittedTarget=JSON.stringify(Camera.Target);SubmittedRevision=VolumeRevision;
        PendingFence = GL.fenceSync(GL.SYNC_GPU_COMMANDS_COMPLETE,0);
        GL.flush();
    }
    return { Upload, Draw, GL };
}
