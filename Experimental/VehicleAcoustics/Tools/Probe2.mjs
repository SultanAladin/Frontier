import { RenderSchedule, MeasureSignal, ComposeStandardSchedule } from "../Source/OfflineRender.js";
for (const Car of ["laFerrari","gt-rNismoGt3","demonSrt","taycanTurbo","viperAcr"]) {
  const t0 = Date.now();
  const R = RenderSchedule(Car, ComposeStandardSchedule(), { Duration: 10.0, SampleRate: 48000, BlockSize: 128 });
  const M = MeasureSignal(R.Left, 48000);
  const Trace = R.Telemetry.filter((_,i)=>i%40===0).map(t=>`${t.Time.toFixed(1)}s ${t.Speed.toFixed(0)}km/h ${t.RPM.toFixed(0)}rpm g${t.Gear} b${t.Boost.toFixed(2)}`).join(" | ");
  console.log(`${Car}: ${(Date.now()-t0)}ms peak=${M.Peak.toFixed(3)} rms=${M.Rms.toFixed(3)} crest=${M.CrestFactor.toFixed(2)}`);
  console.log("   ", Trace);
}
