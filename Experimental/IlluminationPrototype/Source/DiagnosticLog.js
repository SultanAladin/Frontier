// Snapshot only: no console interception, credentials, host/session URLs or per-frame file writes.
export function ConstructDiagnosticLog(Host, Environment = globalThis)
{
    const Durations = Array.from(Host.FrameDurations || []).filter(Value => Value > 0).sort((A, B) => A - B);
    const Percentile = Fraction => Durations.length ? Durations[Math.min(Durations.length - 1, Math.floor(Fraction * Durations.length))] : null;
    return {
        Format: 'Frontier rendering diagnostics v1', CreatedUtc: new Date().toISOString(),
        Browser: Environment.navigator?.userAgent || 'unavailable',
        DevicePixelRatio: Environment.devicePixelRatio || 1,
        Visibility: Environment.document?.visibilityState || 'unknown',
        Adapter: Host.Exchange?.Adapter ? Host.Exchange.QueryAdapterDescription() : 'unavailable',
        Frame: Host.FrameIndex, OutputPixels: Host.ScreenExtent, TracePixels: Host.TraceExtent,
        Settings: JSON.parse(JSON.stringify(Host.Settings || {})),
        FrameTiming: { SmoothedFps: Host.FrameSeconds > 0 ? 1 / Host.FrameSeconds : null,
            SmoothedMs: Host.FrameSeconds > 0 ? Host.FrameSeconds * 1000 : null,
            Samples: Durations.length, MedianMs: Percentile(0.5), P95Ms: Percentile(0.95), P99Ms: Percentile(0.99),
            RawFrameIntervalsMs: Durations },
        Cpu: { LastFrameWorkMs: Host.LastCpuMilliseconds ?? null, LastWheelBlasUpdateMs: Host.WheelRefitMilliseconds ?? null },
        Gpu: { TimestampAvailable: !!Host.Metrics?.Enabled, SampleIndex: Host.Metrics?.SampleIndex || 0,
            LastSubmittedPassMs: { ...Host.Metrics?.LastDurations }, SmoothedPassMs: { ...Host.Metrics?.Durations } },
        Scene: { Instances: Host.RayTracing?.ActiveInstances, TlasNodes: Host.RayTracing?.UsedTlasNodes,
            StoredTriangles: Host.RayTracing?.TotalTriangles, ActualLights: Host.Lighting?.Lights.length,
            Cards: Host.Cards?.Cards.length, ScheduledCardTiles: Host.Cards?.ScheduledTiles,
            RigidBodies: { ...Host.Bodies?.Metrics } },
        Errors: [...(Host.Exchange?.ReportedErrors || [])],
        Comparison: Host.Comparison?.Report || null,
        Note: 'Frame intervals include scheduling/GPU waits, not CPU execution alone. GPU pass samples are asynchronous, not whole-frame time. Smoothed passes may include decaying skipped-pass history. CPU wheel update includes refit/upload submission, not GPU upload completion. No five-source optimality claim.'
    };
}

export async function CopyDiagnosticLog(Host)
{
    const Text = JSON.stringify(ConstructDiagnosticLog(Host), null, 2);
    try
    {
        if (!navigator.clipboard?.writeText) throw new Error('Clipboard API unavailable');
        await navigator.clipboard.writeText(Text);
        Host.Shell?.Announce('Diagnostic log copied');
        return;
    }
    catch { /* Embedded preview permissions can deny clipboard access; offer selectable text. */ }
    const Dialog = document.createElement('dialog');
    Dialog.setAttribute('aria-label', 'Diagnostic log');
    Object.assign(Dialog.style, { width: 'min(760px, 90vw)', background: '#202020', color: '#eee', border: '1px solid #666' });
    const Label = document.createElement('p');
    Label.textContent = 'Clipboard access was denied. Select this log and press Ctrl+C (Cmd+C on Mac).';
    const Field = document.createElement('textarea');
    Field.readOnly = true; Field.value = Text; Field.setAttribute('aria-label', 'Diagnostic log text');
    Object.assign(Field.style, { width: '100%', height: '50vh', color: '#eee', background: '#111' });
    const Close = document.createElement('button'); Close.textContent = 'Close';
    Close.onclick = () => Dialog.close();
    Dialog.addEventListener('close', () => Dialog.remove());
    Dialog.append(Label, Field, Close); document.body.appendChild(Dialog);
    Dialog.showModal(); Field.focus(); Field.select();
}
