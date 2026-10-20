// In-app paired timing capture. GPU figures are measured named-workload subtotals, not whole-frame FPS.
export function SummarizeDurations(Samples)
{
    if (!Samples.length) return null;
    const Sorted = Samples.slice().sort((A, B) => A - B);
    const Percentile = Fraction => Sorted[Math.min(Sorted.length - 1, Math.floor(Fraction * Sorted.length))];
    return { Count: Sorted.length, Median: Percentile(0.5), P95: Percentile(0.95), P99: Percentile(0.99) };
}

export class TimingComparison
{
    constructor(Host)
    {
        this.Host = Host;
        this.Active = false;
        this.Description = "Idle — choose a toggle, then capture A/B";
    }

    Start(Key = "ClusteredLights", Warmup = 60, Frames = 180, Models = ["spring", "shell"])
    {
        if (this.Active) return;
        if (!["ClusteredLights", "ScreenTraversal", "XpbdTyres", "RayTracing", "TyreModel"].includes(Key)) throw new Error("Unknown comparison toggle");
        if (Key === "TyreModel" && (Models.length !== 2 || !Models.every(Model => ["spring", "xpbd", "shell", "solid"].includes(Model))))
            throw new Error("Invalid tyre comparison models");
        this.Models = Models;
        this.TyreComparison = ["TyreModel", "XpbdTyres"].includes(Key);
        this.Key = Key;
        this.Warmup = Warmup;
        this.Frames = Frames;
        this.Saved = { Toggle: this.Host.Settings[Key], Xpbd: this.Host.Settings.XpbdTyres, Frozen: this.Host.Vehicle.Frozen,
            ScriptedInput: this.Host.ScriptedInput,
            Position: this.Host.Vehicle.Position?.slice(),
            Yaw: this.Host.Vehicle.QueryForward ? Math.atan2(this.Host.Vehicle.QueryForward()[1], this.Host.Vehicle.QueryForward()[0]) : 0 };
        this.Host.Vehicle.Frozen = !this.TyreComparison;
        if (this.TyreComparison) this.Host.ScriptedInput = { Throttle: 0, Brake: 0, Steer: 0, Handbrake: false };
        this.Results = [];
        this.Side = 0;
        this.Active = true;
        this.AssignSide();
    }

    AssignSide()
    {
        this.Host.Settings[this.Key] = this.Key === "TyreModel" ? this.Models[this.Side] : this.Side === 1;
        if (this.Key === "TyreModel") this.Host.Settings.XpbdTyres = true;
        if (this.TyreComparison && this.Saved.Position) this.Host.Vehicle.Respawn(this.Saved.Position, this.Saved.Yaw);
        this.Host.ApplySetting(this.Key);
        this.Host.Shell?.Controls.Synchronise();
        this.Count = 0;
        this.Cpu = [];
        this.Gpu = [];
        this.LastSample = this.Host.Metrics.SampleIndex;
        this.Description = `${this.Key}: ${this.Key === "TyreModel" ? this.Models[this.Side] : this.Side ? "ON" : "OFF"} — warming up`;
    }

    Observe(CpuMilliseconds)
    {
        if (!this.Active) return;
        ++this.Count;
        if (this.Count <= this.Warmup) return;
        this.Cpu.push(CpuMilliseconds);
        const Metrics = this.Host.Metrics;
        if (Metrics.Enabled && Metrics.SampleIndex !== this.LastSample)
        {
            this.LastSample = Metrics.SampleIndex;
            this.Gpu.push(Object.values(Metrics.LastDurations).reduce((Sum, Duration) => Sum + Duration, 0));
        }
        this.Description = `${this.Key}: ${this.Key === "TyreModel" ? this.Models[this.Side] : this.Side ? "ON" : "OFF"} — ${this.Cpu.length}/${this.Frames}`;
        if (this.Cpu.length < this.Frames) return;
        this.Results.push({ Model: this.Key === "TyreModel" ? this.Models[this.Side] : undefined, Enabled: this.Side === 1, CpuSubmissionMs: SummarizeDurations(this.Cpu),
                            GpuNamedWorkloadsMs: SummarizeDurations(this.Gpu) });
        if (this.Side === 0) { this.Side = 1; this.AssignSide(); return; }
        this.Active = false;
        this.Host.Settings[this.Key] = this.Saved.Toggle;
        if (this.Key === "TyreModel") this.Host.Settings.XpbdTyres = this.Saved.Xpbd;
        this.Host.Vehicle.Frozen = this.Saved.Frozen;
        this.Host.ScriptedInput = this.Saved.ScriptedInput;
        this.Host.ApplySetting(this.Key);
        this.Host.Shell?.Controls.Synchronise();
        this.Report = { Key: this.Key, Adapter: this.Host.Exchange.QueryAdapterDescription(),
                        ScreenExtent: this.Host.ScreenExtent, TraceExtent: this.Host.TraceExtent,
                        Warmup: this.Warmup, Frames: this.Frames, VehicleFrozen: !this.TyreComparison,
                        Quality: this.Host.GradeName, Results: this.Results,
                        Note: "Fixed-view paired test. CPU submission and named GPU workload subtotals only; not whole-frame FPS." };
        this.Description = "Complete — export timing report; repeat before selecting a winner";
    }

    Download()
    {
        if (!this.Report) return;
        const Link = document.createElement("a");
        const Url = URL.createObjectURL(new Blob([JSON.stringify(this.Report, null, 2)], { type: "application/json" }));
        Link.href = Url;
        Link.download = "Illumination-Timing.json";
        Link.click();
        URL.revokeObjectURL(Url);
    }
}
