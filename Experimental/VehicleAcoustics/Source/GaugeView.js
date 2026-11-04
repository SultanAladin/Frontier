//============================================================================================================================================
//                                                          GAUGEVIEW.JS
//============================================================================================================================================
// 🧩 Drawn instruments: the tachometer with its limiter band and shift light, the live third-octave analyser, and the layer bars that show which
//    acoustic mechanism is currently audible. Pure canvas work — the numbers come from the render worker's telemetry.

const TachArcStart = Math.PI * 0.78;
const TachArcEnd = Math.PI * 2.22;
const TachInk = "#e8edf4";
const TachDim = "#67748a";
const TachRed = "#e4572e";
const TachAmber = "#f4a259";
const TachTeal = "#3ec9c9";

export function DrawTachometer(Canvas, State, Profile)
{
    const Context = Canvas.getContext("2d");
    const Width = Canvas.width;
    const Height = Canvas.height;
    Context.clearRect(0, 0, Width, Height);
    const CentreX = Width * 0.5;
    const CentreY = Height * 1.02;
    const Radius = Height * 0.88;
    const Redline = Profile && Profile.Redline ? Profile.Redline : 7000;
    const Ceiling = Profile && Profile.Limiter ? Profile.Limiter : Redline * 1.03;
    const Scale = Math.max(1000, Math.ceil(Ceiling / 1000) * 1000);
    const AngleFor = (RPM) => TachArcStart + (TachArcEnd - TachArcStart) * Math.max(0.0, Math.min(1.0, RPM / Scale));

    Context.lineCap = "round";
    Context.lineWidth = 14;
    Context.strokeStyle = "#232a36";
    Context.beginPath();
    Context.arc(CentreX, CentreY, Radius, TachArcStart, TachArcEnd);
    Context.stroke();

    // 📝 The red zone is drawn from the authored redline, not from a guess: the limiter band is where the ignition cut actually lives.
    Context.strokeStyle = TachRed;
    Context.beginPath();
    Context.arc(CentreX, CentreY, Radius, AngleFor(Redline), TachArcEnd);
    Context.stroke();

    const RPM = State ? State.RPM : 0.0;
    const Gradient = Context.createLinearGradient(0, 0, Width, 0);
    Gradient.addColorStop(0.0, TachTeal);
    Gradient.addColorStop(0.65, TachAmber);
    Gradient.addColorStop(1.0, TachRed);
    Context.strokeStyle = Gradient;
    Context.lineWidth = 10;
    Context.beginPath();
    Context.arc(CentreX, CentreY, Radius, TachArcStart, AngleFor(RPM));
    Context.stroke();

    Context.strokeStyle = TachDim;
    Context.fillStyle = TachDim;
    Context.font = "12px 'Segoe UI', system-ui, sans-serif";
    Context.textAlign = "center";
    const Step = Scale <= 8000 ? 1000 : (Scale <= 12000 ? 2000 : 2000);
    for (let Tick = 0; Tick <= Scale; Tick += Step)
    {
        const Angle = AngleFor(Tick);
        const Inner = Radius - 18;
        const Outer = Radius - 4;
        Context.lineWidth = 1.5;
        Context.beginPath();
        Context.moveTo(CentreX + Math.cos(Angle) * Inner, CentreY + Math.sin(Angle) * Inner);
        Context.lineTo(CentreX + Math.cos(Angle) * Outer, CentreY + Math.sin(Angle) * Outer);
        Context.stroke();
        Context.fillText(`${Tick / 1000}`, CentreX + Math.cos(Angle) * (Radius - 34),
                         CentreY + Math.sin(Angle) * (Radius - 34) + 4);
    }

    // 📝 Shift light: a filled band lights when the engine is inside 4% of the redline, which is the moment a driver actually shifts.
    if (RPM > Redline * 0.96)
    {
        Context.fillStyle = RPM >= Redline ? TachRed : TachAmber;
        const Angle = AngleFor(RPM);
        Context.beginPath();
        Context.arc(CentreX + Math.cos(Angle) * Radius, CentreY + Math.sin(Angle) * Radius, 7, 0, Math.PI * 2);
        Context.fill();
    }
}

export function DrawAnalyser(Canvas, Bins, SampleRate, FiringFrequency)
{
    const Context = Canvas.getContext("2d");
    const Width = Canvas.width;
    const Height = Canvas.height;
    Context.clearRect(0, 0, Width, Height);
    Context.fillStyle = "rgba(12, 16, 24, 0.55)";
    Context.fillRect(0, 0, Width, Height);

    // 📝 Logarithmic frequency axis from 20 Hz to 16 kHz: a linear axis shows nothing but the top octave of an engine note.
    const Low = 20.0;
    const High = 16000.0;
    const BinCount = Bins ? Bins.length : 0;
    const Nyquist = SampleRate * 0.5;
    Context.strokeStyle = "rgba(103, 116, 138, 0.22)";
    Context.lineWidth = 1;
    Context.font = "10px 'Segoe UI', system-ui, sans-serif";
    Context.fillStyle = TachDim;
    for (const Frequency of [50, 100, 200, 500, 1000, 2000, 5000, 10000])
    {
        const X = (Math.log(Frequency / Low) / Math.log(High / Low)) * Width;
        Context.beginPath();
        Context.moveTo(X, 0);
        Context.lineTo(X, Height - 12);
        Context.stroke();
        Context.fillText(Frequency >= 1000 ? `${Frequency / 1000}k` : `${Frequency}`, X + 2, Height - 2);
    }

    if (BinCount > 0)
    {
        Context.beginPath();
        Context.moveTo(0, Height);
        for (let X = 0; X <= Width; ++X)
        {
            const Frequency = Low * Math.pow(High / Low, X / Width);
            const Bin = Math.min(BinCount - 1, Math.max(0, Math.round(Frequency / Nyquist * BinCount)));
            const Magnitude = Bins[Bin] / 255.0;
            const Y = (Height - 14) * (1.0 - Math.pow(Magnitude, 1.35));
            Context.lineTo(X, Y);
        }
        Context.lineTo(Width, Height);
        Context.closePath();
        const Gradient = Context.createLinearGradient(0, 0, 0, Height);
        Gradient.addColorStop(0.0, "rgba(244, 162, 89, 0.85)");
        Gradient.addColorStop(1.0, "rgba(62, 201, 201, 0.10)");
        Context.fillStyle = Gradient;
        Context.fill();
    }

    // 📝 Firing-order marker: the vertical line is where the engine's fundamental actually is, so the picture can be checked against the maths.
    if (FiringFrequency > Low && FiringFrequency < High)
    {
        const X = (Math.log(FiringFrequency / Low) / Math.log(High / Low)) * Width;
        Context.strokeStyle = TachTeal;
        Context.setLineDash([3, 4]);
        Context.beginPath();
        Context.moveTo(X, 0);
        Context.lineTo(X, Height - 14);
        Context.stroke();
        Context.setLineDash([]);
        Context.fillStyle = TachTeal;
        Context.font = "11px 'Segoe UI', system-ui, sans-serif";
        Context.fillText(`firing ${FiringFrequency.toFixed(0)} Hz`, Math.min(X + 4, Width - 90), 13);
    }
}

// 📝 The layer list is derived from the telemetry rather than hardcoded: each bar is the physical driver of that layer at this instant. A bar
//    that is silent when its mechanism is inactive is the point — it shows the model is not one oscillator wearing nine hats.
export function ComposeLayerRows(State, Profile)
{
    if (!State || !Profile) return [];
    const Clamp01 = (Value) => Math.max(0.0, Math.min(1.0, Value));
    const Rows = [
        { Name: "Exhaust pulse train", Detail: `${Profile.Cylinders > 0 ? Profile.Cylinders + " cyl" : "—"} · ${State.FiringHz.toFixed(0)} Hz`,
          Value: Profile.Electric ? 0.0 : Clamp01(0.20 + State.Throttle * 0.55 + State.Overrun * 0.35) },
        { Name: "Bypass valve", Detail: `${(State.Bypass * 100).toFixed(0)}% open`,
          Value: Profile.Electric ? 0.0 : Clamp01(State.Bypass) },
        { Name: "Overrun crackle", Detail: `${(State.Overrun * 100).toFixed(0)}% unburnt`,
          Value: Profile.Electric ? 0.0 : Clamp01(State.Overrun) },
        { Name: "Ignition cut", Detail: State.Limiter > 0.1 ? "limiter" : (State.ShiftCut > 0.1 ? "shift cut" : "—"),
          Value: Profile.Electric ? 0.0 : Clamp01(Math.max(State.Limiter, State.Misfire)) },
        { Name: "Compressor", Detail: Profile.Induction === "twinScrollTurbo" ? `${(State.Boost || 0).toFixed(2)} bar` : "not fitted",
          Value: Profile.Induction === "twinScrollTurbo" ? Clamp01(State.BoostFraction + State.BlowOff * 0.6) : 0.0 },
        { Name: "Blower mesh", Detail: Profile.Induction === "rootsSupercharger" ? `${(State.Blower * 100).toFixed(0)}% rotor` : "not fitted",
          Value: Profile.Induction === "rootsSupercharger" ? Clamp01(State.Blower) : 0.0 },
        { Name: "Drive whine", Detail: Profile.Electric ? `${State.RPM.toFixed(0)} rpm` : "not fitted",
          Value: Profile.Electric ? Clamp01(State.RPM / Math.max(1, Profile.Redline)) : 0.0 },
        { Name: "Gearbox mesh", Detail: State.Gear > 0 ? `gear ${State.Gear}` : "neutral",
          Value: Clamp01(0.25 + State.Throttle * 0.5) },
        { Name: "Tyre scrub", Detail: `${(State.Scrub * 100).toFixed(0)}%`,
          Value: Clamp01(State.Scrub) },
        { Name: "Driveline lash", Detail: State.Lash > 0.1 ? "take-up" : "—",
          Value: Clamp01(State.Lash) },
        { Name: "Wind and road", Detail: `${State.Speed.toFixed(0)} km/h`,
          Value: Clamp01(Math.pow(State.Speed / 220.0, 1.7) * 1.6) },
        { Name: "Cooling fan", Detail: State.Fan > 0.5 ? "on" : "off",
          Value: Clamp01(State.Fan) }
    ];
    return Rows;
}

export function DrawLayerBars(Container, Rows)
{
    if (!Container) return;
    if (Container.childElementCount !== Rows.length)
    {
        Container.textContent = "";
        for (const Row of Rows)
        {
            const Element = document.createElement("div");
            Element.className = "layer-row";
            Element.innerHTML = `<span class="layer-name"></span><span class="layer-detail"></span>` +
                                `<span class="layer-track"><i></i></span>`;
            Container.appendChild(Element);
        }
    }
    for (let Index = 0; Index < Rows.length; ++Index)
    {
        const Element = Container.children[Index];
        Element.querySelector(".layer-name").textContent = Rows[Index].Name;
        Element.querySelector(".layer-detail").textContent = Rows[Index].Detail;
        const Fill = Element.querySelector(".layer-track i");
        Fill.style.width = `${(Rows[Index].Value * 100).toFixed(1)}%`;
        Element.classList.toggle("active", Rows[Index].Value > 0.06);
    }
}
