import {OccupantStride} from './FacetStructure.js';
import {SaveEnvironment} from './EnvironmentPersistence.js';
const Axes=['X','Y','Z'];
export function ResolveEditorTransform(H,Id){
 const S=H.Settings;
 if(!Id)return null;
 const Prefix=Id==='local-cloud'?'LocalCloud':Id==='local-fog'?'LocalFog':null;
 if(Prefix)return {Get:()=>Axes.map(A=>S[Prefix+A]),Set:P=>{Axes.forEach((A,I)=>S[Prefix+A]=P[I]);SaveEnvironment(S);},GetScale:()=>Axes.map(A=>S[Prefix+'Half'+A]),SetScale:P=>{Axes.forEach((A,I)=>S[Prefix+'Half'+A]=Math.max(1,P[I]));SaveEnvironment(S);}};
 if(['vehicle','cabin','marks'].includes(Id))return {Get:()=>H.Vehicle.Position.slice(),Set:P=>H.Vehicle.Respawn(P,Math.atan2(2*(H.Vehicle.Orientation[3]*H.Vehicle.Orientation[2]+H.Vehicle.Orientation[0]*H.Vehicle.Orientation[1]),1-2*(H.Vehicle.Orientation[1]**2+H.Vehicle.Orientation[2]**2)))};
 const Cone=Id.startsWith('cone-')?H.Cones.Cones[Number(Id.slice(5))]:null;
 if(Cone)return {Get:()=>Cone.Body.Position.slice(),Set:P=>{H.Bodies.PlaceBody(Cone.Body,P,Cone.Body.Orientation,true);Cone.Body.Moved=true;}};
 const Prop=Id.startsWith('prop-')?H.Fracture.Props.find(P=>P.Index===Number(Id.slice(5))):null;
 const Index=Prop?Prop.Definition.IntactOccupant:Id.startsWith('object-')?Number(Id.slice(7)):null;
 if(Index!==null&&H.Scene.Occupants[Index]){
  const Body=H.Bodies.Bodies.find(B=>B.Owner?.Occupant===Index);if(Body)return {Get:()=>Body.Position.slice(),Set:P=>H.Bodies.PlaceBody(Body,P,Body.Orientation,true)};
  const O=H.Scene.Occupants[Index];const Centre=()=>{const B=H.Scene.QueryWorldBounds(Index);return B.Minimum.map((V,I)=>(V+B.Maximum[I])*.5);};return {Get:Centre,Set:P=>{
   const Old=H.Scene.QueryWorldBounds(Index),T=O.Transform.slice(),Delta=P.map((V,I)=>V-Centre()[I]);T.set(Delta.map((V,I)=>T[12+I]+V),12);H.Scene.AssignOccupantTransform(Index,T);
   if(Prop){Prop.Definition.Centre=Prop.Definition.Centre.map((V,I)=>V+Delta[I]);for(const Piece of Prop.Pieces??[])if(Piece.Body)H.Bodies.PlaceBody(Piece.Body,Piece.Body.Position.map((V,I)=>V+Delta[I]),Piece.Body.Orientation,true);}
   H.Exchange.Device.queue.writeBuffer(H.OccupantRecords,Index*OccupantStride,H.Scene.EncodeOccupantRange(H.Volumes,Index,1));H.Distance.InvalidateBounds([Old,H.Scene.QueryWorldBounds(Index)]);H.Cards.InvalidateLighting();
  },Commit:()=>{const Boxes=H.Scene.QueryObstacleBounds();H.Vehicle.AssignObstacles(Boxes);H.Bodies.AssignObstacles(Boxes);}};
 }
 return null;
}
export function ConstructSpatialEntries(H){
 const Entries=[];const Covered=new Set([H.Scene.VehicleOccupants.Body,...H.Scene.VehicleOccupants.Wheels,...H.Cones.Cones.map(C=>C.Occupant),...H.Fracture.Props.map(P=>P.Definition.IntactOccupant)]);
 for(const Cone of H.Cones.Cones)Entries.push({Id:`cone-${Cone.Index}`,Name:Cone.Name||`Cone ${Cone.Index}`,Group:'Objects',Type:'Rigid body',Glyph:'TrafficCone',Description:'Movable cone',State:()=>'',Build:(C,P)=>P.appendChild(Object.assign(document.createElement('p'),{textContent:'Use the viewport translation gizmo to move this cone.'}))});
 H.Scene.Occupants.forEach((O,I)=>{if(Covered.has(I)||O.Inactive||!(O.Roles&1)||I===H.Scene.GroundOccupant)return;Entries.push({Id:`object-${I}`,Name:O.Name||`Object ${I}`,Group:'Objects',Type:'Scene object',Glyph:'Box',Description:'Scene transform',State:()=>'',Build:(C,P)=>P.appendChild(Object.assign(document.createElement('p'),{textContent:'Use the viewport translation gizmo to move this object. Collision bounds refresh on release.'}))});});return Entries;
}

export function BuildTransformInspector(H,Id,C,B){
 const T=ResolveEditorTransform(H,Id);if(!T)return;
 const Card=C.Card(B,'World transform','Move',{Accent:'#78aaff'});
 C.Muted(Card,'Demo-style viewport handles: drag axes/plane pads, or G then X/Y/Z. Ctrl snaps; Enter confirms; Esc restores; F frames selection. S scales local weather volumes.');
 const Row=document.createElement('div');Row.className='transform-fields';Card.appendChild(Row);
 const Fields=Axes.map((Axis,I)=>{const L=document.createElement('label');L.textContent=Axis;const Input=document.createElement('input');Input.type='number';Input.step='.25';Input.style.width='100%';Input.setAttribute('aria-label','World '+Axis);Input.addEventListener('change',()=>{const Value=Number(Input.value);if(!Number.isFinite(Value))return;const P=T.Get();P[I]=Value;T.Set(P);T.Commit?.();H.Telemetry?.Event('object_transform',{object:Id,position:P});});L.appendChild(Input);Row.appendChild(L);return Input;});
 const Sync=()=>Fields.forEach((F,I)=>{if(document.activeElement!==F)F.value=T.Get()[I].toFixed(2);});C.Live.push(Sync);Sync();
 C.Button(Card,'Frame selection','Scan',()=>H.Shell.Gizmo?.FrameSelection());
}
