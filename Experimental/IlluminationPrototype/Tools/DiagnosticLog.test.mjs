import test from 'node:test';
import assert from 'node:assert/strict';
import { ConstructDiagnosticLog } from '../Source/DiagnosticLog.js';
test('diagnostic snapshot includes comparable settings, timings, errors and counts without session URLs', () => {
    const Host = { Settings:{RayTracing:true,MaxBounces:3}, FrameDurations:[0,50,40,60], FrameSeconds:0.05,
        WheelRefitMilliseconds:9, LastCpuMilliseconds:12,
        Metrics:{Enabled:true,SampleIndex:4,LastDurations:{Direct:25},Durations:{Direct:24}},
        Exchange:{Adapter:{},QueryAdapterDescription:()=> 'Test GPU',ReportedErrors:['test error']},
        ScreenExtent:[1920,1080],TraceExtent:[960,540],FrameIndex:42 };
    const Result = ConstructDiagnosticLog(Host, {navigator:{userAgent:'Test browser'},location:{href:'private-session'}});
    assert.equal(Result.FrameTiming.SmoothedFps,20);
    assert.equal(Result.FrameTiming.MedianMs,50);
    assert.equal(Result.Cpu.LastWheelBlasUpdateMs,9);
    assert.equal(Result.Gpu.LastSubmittedPassMs.Direct,25);
    assert.deepEqual(Result.Errors,['test error']);
    assert.equal(JSON.stringify(Result).includes('private-session'),false);
    Host.Settings.MaxBounces=1; Host.Exchange.ReportedErrors.push('later');
    assert.equal(Result.Settings.MaxBounces,3); assert.equal(Result.Errors.length,1);
});
test('diagnostics distinguish missing timings from measured zero', () => {
    const Result = ConstructDiagnosticLog({}, {});
    assert.equal(Result.Cpu.LastWheelBlasUpdateMs,null);
    assert.equal(Result.FrameTiming.MedianMs,null);
    assert.equal(Result.Gpu.TimestampAvailable,false);
});
