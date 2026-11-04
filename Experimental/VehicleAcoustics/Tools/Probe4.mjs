import { RenderSchedule } from "../Source/OfflineRender.js";
const Sched = [{Seconds:0.5,Throttle:0},{Seconds:6.0,Throttle:1.0}];
for (const Car of ["mustangGt","laFerrari","taycanTurbo"]) {
  const R = RenderSchedule(Car, Sched, { Duration: 6.5, SampleRate: 48000, BlockSize: 128 });
  const B = R.Bundle.Condition;
  const T = R.Telemetry;
  const rows = [];
  for (let k=0;k<T.length;k+=Math.round(T.length/13)) rows.push(`${T[k].Time.toFixed(1)}s ${T[k].Speed.toFixed(0)}k ${T[k].RPM.toFixed(0)}r g${T[k].Gear}`);
  console.log(Car, "peak", R.Left.reduce((a,b)=>Math.max(a,Math.abs(b)),0).toFixed(2));
  console.log("  ", rows.join(" | "));
  console.log("   final condition:", Object.fromEntries(["Speed","WheelSpeed","EngineAngularSpeed","ClutchEngagement","ClutchSlipSpeed","LockupState","WheelSlipRatio","TransmittedTorque"].map(k=>[k,+B[k].toFixed(2)])));
}
