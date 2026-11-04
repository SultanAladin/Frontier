import { RenderSchedule, MeasureSignal, ComposeStandardSchedule } from "../Source/OfflineRender.js";
for (const Car of ["laFerrari","gt-rNismoGt3","mustangGt","ageraRs","spyder918","taycanTurbo","demonSrt","viperAcr","challengerScat"]) {
  const R = RenderSchedule(Car, ComposeStandardSchedule(), { Duration: 10.0, SampleRate: 48000, BlockSize: 128 });
  const M = MeasureSignal(R.Left, 48000);
  const T = R.Telemetry;
  const at = (s)=>T[Math.min(T.length-1, Math.round(s*T.length/10))];
  const gears = T.map(t=>t.Gear);
  const maxGear = Math.max(...gears), minGear = Math.min(...gears);
  const maxRpm = Math.max(...T.map(t=>t.RPM)), maxSpeed = Math.max(...T.map(t=>t.Speed));
  const topSpeed = T.Telemetry;
  console.log(`${Car.padEnd(15)} peak=${M.Peak.toFixed(2)} rms=${M.Rms.toFixed(3)} crest=${M.CrestFactor.toFixed(1)} | rpmMax=${maxRpm.toFixed(0)} speedMax=${maxSpeed.toFixed(0)} gears=${minGear}..${maxGear} | 0.0s:${at(0).RPM.toFixed(0)}r 2.5s:${at(2.5).Speed.toFixed(0)}km/h g${at(2.5).Gear} 5s:${at(5).Speed.toFixed(0)}km/h g${at(5).Gear} ${at(5).RPM.toFixed(0)}r 9.9s:${at(9.9).Speed.toFixed(0)}km/h`);
}
