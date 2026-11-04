import { RenderSchedule, MeasureSignal, MeasureDominantOrder, ComposeStandardSchedule } from "../Source/OfflineRender.js";
const t0 = Date.now();
const R = RenderSchedule("laFerrari", ComposeStandardSchedule(), { Duration: 3.0, SampleRate: 48000, BlockSize: 128 });
console.log(R.Report);
console.log("render ms:", Date.now() - t0, "samples:", R.Left.length);
console.log("measure:", MeasureSignal(R.Left, 48000));
