// Demo-inspired screen-space fallback gizmo. Z-up, renderer/physics-backed translation adapters.
import {ResolveEditorTransform} from './EditorTransforms.js';
const NS='http://www.w3.org/2000/svg',Colours=['#f16e6e','#75dc83','#78aaff'];
const Dot=(A,B)=>A.reduce((S,V,I)=>S+V*B[I],0);
export function ProjectGizmo(P,C,W,H){const V=P.map((X,I)=>X-C.Position[I]),D=Dot(V,C.Forward);if(D<=.05)return null;return [W/2+Dot(V,C.Right)/D/C.TanY*H/2,H/2-Dot(V,C.Up)/D/C.TanY*H/2,D];}
export class TransformGizmo{
 constructor(Shell){this.Shell=Shell;this.Host=Shell.Host;this.Mouse=[0,0];this.Mode='move';const Stage=document.getElementById('stage');if(!Stage||!document.createElementNS)return;
 this.SVG=document.createElementNS(NS,'svg');Object.assign(this.SVG.style,{position:'absolute',inset:'0',width:'100%',height:'100%',zIndex:12,pointerEvents:'none',overflow:'hidden'});Stage.appendChild(this.SVG);
 this.Hud=document.createElement('div');Object.assign(this.Hud.style,{position:'absolute',bottom:'12px',left:'12px',zIndex:13,background:'#171d23dd',color:'#d5dce6',padding:'7px 12px',borderRadius:'8px',fontSize:'11px'});Stage.appendChild(this.Hud);
 this.SVG.addEventListener('pointerdown',E=>{const Axis=E.target.dataset.axis;if(Axis===undefined)return;E.preventDefault();E.stopPropagation();this.Begin(E,Axis==='free'?null:Axis.startsWith('p')?Axis:Number(Axis));if(this.Drag)this.SVG.setPointerCapture(E.pointerId);});
 window.addEventListener('pointermove',E=>{this.Mouse=[E.clientX,E.clientY];if(this.Drag){E.preventDefault();E.stopImmediatePropagation();this.Move(E);}},true);
 window.addEventListener('pointerup',()=>{if(this.Drag&&!this.Drag.Modal)this.End();},true);
 this.SVG.addEventListener('pointercancel',()=>this.Cancel());
 window.addEventListener('keydown',E=>{if(/INPUT|TEXTAREA|SELECT/.test(E.target.tagName)||E.target.isContentEditable)return;const K=E.key.toLowerCase();if(this.Drag){if(K==='escape'){this.Cancel();}else if(K==='enter'){this.End();}else if(['x','y','z'].includes(K)){this.Drag.Axis=['x','y','z'].indexOf(K);}else return;E.preventDefault();E.stopImmediatePropagation();return;}
 if(K==='s'&&this.Target()?.GetScale){this.Begin({clientX:this.Mouse[0],clientY:this.Mouse[1]},null);if(!this.Drag)return;this.Drag.Scale=this.Drag.T.GetScale();if(!this.Drag)return;this.Drag.Modal=true;E.preventDefault();E.stopImmediatePropagation();}
 if(K==='g'&&this.Target()){this.Begin({clientX:this.Mouse[0],clientY:this.Mouse[1]},null);if(!this.Drag)return;this.Drag.Modal=true;E.preventDefault();E.stopImmediatePropagation();}
 if(K==='f'&&this.Target()){this.FrameSelection();E.preventDefault();E.stopImmediatePropagation();}
 },true);
 window.addEventListener('pointerdown',E=>{if(this.Drag?.Modal){E.preventDefault();E.stopImmediatePropagation();E.button===2?this.Cancel():this.End();}},true);
 window.addEventListener('blur',()=>this.Cancel());
 }
 FrameSelection(){const T=this.Target();if(!T)return;const P=T.Get();this.Host.FreeCamera=true;this.Host.Flight.Eye=P.map((V,I)=>V+[-18,-18,12][I]);this.Host.Flight.Yaw=Math.PI/4;this.Host.Flight.Pitch=-Math.atan2(12,Math.hypot(18,18));}
 Target(){return ResolveEditorTransform(this.Host,this.Shell.Selected);}
 Begin(E,Axis){const T=this.Target();if(!T||!this.Host.CameraState)return;this.Drag={T,Axis,Start:T.Get(),Mouse:[E.clientX,E.clientY],Camera:structuredClone(this.Host.CameraState),Id:this.Shell.Selected};}
 Move(E){const D=this.Drag;if(D.Id!==this.Shell.Selected){this.Cancel();return;}const B=this.SVG.getBoundingClientRect(),P=ProjectGizmo(D.Start,D.Camera,B.width,B.height);if(!P)return;const DX=E.clientX-D.Mouse[0],DY=E.clientY-D.Mouse[1],PerPixel=2*P[2]*D.Camera.TanY/B.height;let Delta;
 if(D.Axis===null)Delta=D.Start.map((_,I)=>(D.Camera.Right[I]*DX-D.Camera.Up[I]*DY)*PerPixel);
 else if(typeof D.Axis==='string'){
 const A=Number(D.Axis[1]),U=(A+1)%3,V=(A+2)%3;
 const Q=ProjectGizmo(D.Start.map((N,I)=>N+(I===U?1:0)),D.Camera,B.width,B.height),R=ProjectGizmo(D.Start.map((N,I)=>N+(I===V?1:0)),D.Camera,B.width,B.height);if(!Q||!R)return;
 const a=Q[0]-P[0],b=R[0]-P[0],c=Q[1]-P[1],d=R[1]-P[1],Det=a*d-b*c;if(Math.abs(Det)<1e-6)return;Delta=[0,0,0];Delta[U]=(DX*d-b*DY)/Det;Delta[V]=(a*DY-c*DX)/Det;
 }
 else{const Unit=[0,0,0];Unit[D.Axis]=1;const Q=ProjectGizmo(D.Start.map((V,I)=>V+Unit[I]),D.Camera,B.width,B.height);if(!Q)return;const X=Q[0]-P[0],Y=Q[1]-P[1],Len=X*X+Y*Y;if(Len<1e-6)return;Delta=Unit.map(V=>V*(DX*X+DY*Y)/Len);}
 if(D.Scale){const Factor=Math.max(.01,Math.exp((DX-DY)*.01));D.T.SetScale(D.Scale.map((V,I)=>D.Axis===null||D.Axis===I?V*Factor:V));this.Shell.Controls.Synchronise();this.Draw();return;}
 const Next=D.Start.map((V,I)=>{const N=V+Delta[I];return E.ctrlKey?Math.round(N):N;});D.T.Set(Next);this.Shell.Controls.Synchronise();this.Draw();
 }
 End(){if(this.Drag){this.Drag.T.Commit?.();this.Host.Telemetry?.Event('object_transform',{object:this.Drag.Id,position:this.Drag.T.Get(),scale:this.Drag.T.GetScale?.()});}this.Drag=null;}
 Cancel(){if(this.Drag){if(this.Drag.Scale)this.Drag.T.SetScale(this.Drag.Scale);else this.Drag.T.Set(this.Drag.Start);this.Drag.T.Commit?.();this.Drag=null;}}
 Draw(){if(!this.SVG)return;const T=this.Target(),C=this.Host.CameraState,B=this.SVG.getBoundingClientRect();const Key=JSON.stringify([this.Shell.Selected,T?.Get(),C,B.width,B.height]);if(Key===this.DrawKey)return;this.DrawKey=Key;this.SVG.replaceChildren();this.Hud.hidden=!T;if(!T||!C)return;this.Hud.textContent='Move: G · X/Y/Z · Ctrl snap · Esc cancel · F frame'+(T.GetScale?' · S scale':'');const O=T.Get(),P=ProjectGizmo(O,C,B.width,B.height);if(!P)return;const L=P[2]*C.TanY*.24;
 for(let I=0;I<3;I++){const V=O.slice();V[I]+=L;const Q=ProjectGizmo(V,C,B.width,B.height);if(!Q||Math.hypot(Q[0]-P[0],Q[1]-P[1])<12)continue;const Line=document.createElementNS(NS,'line');for(const [K,V]of Object.entries({x1:P[0],y1:P[1],x2:Q[0],y2:Q[1],stroke:Colours[I],'stroke-width':5,'stroke-linecap':'round'}))Line.setAttribute(K,V);Line.dataset.axis=I;Line.style.pointerEvents='stroke';Line.style.cursor='grab';this.SVG.appendChild(Line);const Text=document.createElementNS(NS,'text');Text.setAttribute('x',Q[0]+6);Text.setAttribute('y',Q[1]-5);Text.setAttribute('fill',Colours[I]);Text.setAttribute('font-size',13);Text.textContent=['X','Y','Z'][I];this.SVG.appendChild(Text);}
 for(let A=0;A<3;A++){const U=(A+1)%3,V=(A+2)%3,Points=[[.2,.2],[.4,.2],[.4,.4],[.2,.4]].map(([X,Y])=>ProjectGizmo(O.map((N,I)=>N+L*((I===U?X:0)+(I===V?Y:0))),C,B.width,B.height));if(Points.some(Q=>!Q))continue;const Pad=document.createElementNS(NS,'polygon');Pad.setAttribute('points',Points.map(Q=>Q.slice(0,2).join(',')).join(' '));Pad.setAttribute('fill',Colours[A]);Pad.setAttribute('opacity','.35');Pad.dataset.axis='p'+A;Pad.style.pointerEvents='all';Pad.style.cursor='move';this.SVG.appendChild(Pad);}
 const Centre=document.createElementNS(NS,'circle');Centre.setAttribute('cx',P[0]);Centre.setAttribute('cy',P[1]);Centre.setAttribute('r',7);Centre.setAttribute('fill','#e2e5e7');Centre.dataset.axis='free';Centre.style.pointerEvents='all';Centre.style.cursor='move';this.SVG.appendChild(Centre);
 }
}
