// Matched tiny software-GPU regression probe, not a target-hardware FPS benchmark.
import fs from 'node:fs';
const Samples = [], Seen = new Set();
function Collect(Host) {
    if (Host.Metrics.SampleIndex && !Seen.has(Host.Metrics.SampleIndex)) {
        Seen.add(Host.Metrics.SampleIndex);
        if (Host.FrameIndex >= 3) Samples.push({...Host.Metrics.LastDurations});
    }
}
export default {
    BeforeFrame(Host, Frame) {
        if (Frame === 0) {
            Host.Settings.XpbdTyres = false; Host.Settings.TyreModel = 'spring';
            Host.Vehicle.XpbdEnabled = false; Host.Vehicle.TyreModel = 'spring';
            Host.Vehicle.Advance({Throttle:0,Brake:0,Steer:0,Handbrake:false},1/60);
            Host.Vehicle.Frozen = true;
            Host.Settings.ScreenTraversal = false; Host.Settings.ClusteredLights = false; Host.Settings.PhotometricLights = false;
            Host.Settings.RayTracing = true;
        }
        Collect(Host);
    },
    Report(Host) {
        Collect(Host);
        const Result = {Adapter:Host.Exchange.QueryAdapterDescription(), Screen:Host.ScreenExtent,Trace:Host.TraceExtent,
            Samples, Triangles:Host.RayTracing.TotalTriangles, Occupants:Host.Scene.Occupants.length,
            Errors:Host.Exchange.ReportedErrors, Note:'Fixed pose, spring model, screen/IES/clustering OFF, low quality, 1 bounce/candidate. Tiny software GPU regression probe; no hardware FPS claim.'};
        if (process.env.BENCH_OUT) fs.writeFileSync(process.env.BENCH_OUT,JSON.stringify(Result,null,2));
        console.log(JSON.stringify(Result));
    }
};
