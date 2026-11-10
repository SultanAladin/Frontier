//============================================================================================================================================
//                                                             SLIDERCONTROL.JS
//============================================================================================================================================
// 🧩 Canonical Frontier Editor slider: two-cell black/inset value capsule above a full-width 22 px neutral pill track.

// 📝 Unifies ControlKit::Slider and ControlKit::ValuePill (Engine/Interface/ControlKit.cpp) with the browser inspector:
//    ① QuantizeSliderScalar snaps relative to Minimum and rounds to the exact decimal places of Minimum/Step (zero IEEE-754 drift);
//    ② Refresh skips overwriting the numeric input while focused so live typing is never clobbered by 5 Hz inspector sync;
//    ③ --slider-ratio drives thumb-centre-aligned fill (calc(10px + ratio * (100% - 20px))) so fill never bleeds past the knob;
//    ④ Double-clicking the track restores the initial scalar; dragging the label scrubs horizontally (Shift = 0.1× fine).

function CountDecimalPlaces(Scalar)
{
    const Text = String(Scalar).toLowerCase();
    const [Mantissa, Exponent = "0"] = Text.split("e");
    const FractionLength = Mantissa.split(".")[1]?.length ?? 0;
    return Math.max(0, FractionLength - Number(Exponent));
}

export function ResolveSliderDigits(Minimum, Step)
{
    return Math.min(10, Math.max(CountDecimalPlaces(Minimum), CountDecimalPlaces(Step)));
}

export function QuantizeSliderScalar(Scalar, Minimum, Maximum, Step)
{
    if (!Number.isFinite(Scalar))
    {
        return Minimum;
    }
    const SafeStep = Step > 0 ? Step : 0.01;
    const Snapped  = Minimum + Math.round((Scalar - Minimum) / SafeStep) * SafeStep;
    const Clamped  = Math.max(Minimum, Math.min(Maximum, Snapped));
    return Number(Clamped.toFixed(ResolveSliderDigits(Minimum, SafeStep)));
}

function AssignVariable(Node, Name, Text)
{
    if (Node.style && Node.style.setProperty)
    {
        Node.style.setProperty(Name, Text);
    }
    else if (Node.style)
    {
        Node.style[Name] = Text;
    }
}

function MakeNode(Tag, ClassName = "", Text = "")
{
    const Node = document.createElement(Tag);
    if (ClassName)
    {
        Node.className = ClassName;
    }
    if (Text)
    {
        Node.textContent = Text;
    }
    return Node;
}

export function ConstructSliderControl(Container, Target, Key, Specification)
{
    const Minimum = Number(Specification.Minimum ?? 0);
    const Maximum = Number(Specification.Maximum ?? 1);
    const Step    = Number(Specification.Step ?? 0.01);
    const Scale   = Number(Specification.Scale ?? 1.0);
    const Unit    = Specification.Unit || "";
    const Digits  = Specification.Digits !== undefined
                  ? Specification.Digits
                  : ResolveSliderDigits(Minimum * Scale, Step * Scale);
    const Initial = Number.isFinite(Number(Target[Key])) ? Number(Target[Key]) : Minimum;

    const Root        = MakeNode("div", "frontier-slider control");
    const Head        = MakeNode("div", "frontier-slider-head");
    const Caption     = MakeNode("span", "frontier-slider-label", Specification.Label || Key);
    const Pill        = MakeNode("label", `frontier-slider-value${Unit ? " has-unit" : ""}`);
    const NumberInput = MakeNode("input", "frontier-slider-number");
    NumberInput.type      = "text";
    NumberInput.inputMode = "decimal";
    NumberInput.setAttribute("aria-label", `${Specification.Label || Key} numeric`);
    Pill.appendChild(NumberInput);

    if (Unit)
    {
        Pill.appendChild(MakeNode("span", "frontier-slider-unit", Unit));
    }

    const Track = MakeNode("input", "frontier-slider-track");
    Track.type = "range";
    Track.min  = String(Minimum);
    Track.max  = String(Maximum);
    Track.step = String(Step);
    Track.setAttribute("aria-label", Specification.Label || Key);
    Track.title = "Drag to adjust · Double-click to reset";

    Head.append(Caption, Pill);
    Root.append(Head, Track);

    if (Specification.Labels)
    {
        const RangeEnds = MakeNode("div", "range-labels");
        RangeEnds.append(MakeNode("span", "", Specification.Labels[0]), MakeNode("span", "", Specification.Labels[1]));
        Root.appendChild(RangeEnds);
    }
    Container.appendChild(Root);

    const Span = Math.max(Maximum - Minimum, 1.0e-9);

    const Refresh = () =>
    {
        const Current = Number.isFinite(Number(Target[Key])) ? Number(Target[Key]) : Minimum;
        const Ratio   = Math.max(0.0, Math.min(1.0, (Current - Minimum) / Span));
        Track.value   = String(Current);
        if (typeof document === "undefined" || document.activeElement !== NumberInput)
        {
            NumberInput.value = (Current * Scale).toFixed(Digits);
        }
        AssignVariable(Root, "--slider-ratio", Ratio.toFixed(6));
        AssignVariable(Root, "--slider-fill", `${(Ratio * 100.0).toFixed(3)}%`);
    };

    const Finalize = (Candidate) =>
    {
        if (!Number.isFinite(Candidate))
        {
            return;
        }
        const Next = QuantizeSliderScalar(Candidate, Minimum, Maximum, Step);
        Target[Key] = Next;
        Refresh();
        if (Specification.OnInput)
        {
            Specification.OnInput(Next);
        }
        if (Specification.OnFinalize)
        {
            Specification.OnFinalize(Key, Target);
        }
    };

    Track.addEventListener("input", () => Finalize(Number(Track.value)));
    Track.addEventListener("dblclick", () => Finalize(Initial));

    let PriorBeforeEdit = Initial;
    NumberInput.addEventListener("focus", () =>
    {
        PriorBeforeEdit = Number.isFinite(Number(Target[Key])) ? Number(Target[Key]) : Minimum;
        if (NumberInput.select)
        {
            NumberInput.select();
        }
    });

    const ParseTypedScalar = () =>
    {
        const Raw = String(NumberInput.value ?? "").trim().replace(/[^0-9eE+\-.]/g, "");
        if (Raw === "" || Raw === "-" || Raw === "+" || Raw === ".")
        {
            return NaN;
        }
        return Number(Raw) / Scale;
    };

    NumberInput.addEventListener("change", () =>
    {
        const Parsed = ParseTypedScalar();
        if (Number.isFinite(Parsed))
        {
            Finalize(Parsed);
        }
        NumberInput.value = ((Number(Target[Key] ?? Minimum)) * Scale).toFixed(Digits);
    });

    NumberInput.addEventListener("blur", () =>
    {
        NumberInput.value = ((Number(Target[Key] ?? Minimum)) * Scale).toFixed(Digits);
    });

    NumberInput.addEventListener("keydown", (Event) =>
    {
        if (Event.key === "ArrowUp" || Event.key === "ArrowDown")
        {
            Event.preventDefault();
            const Direction  = Event.key === "ArrowUp" ? 1.0 : -1.0;
            const Multiplier = Event.shiftKey ? 10.0 : 1.0;
            const Next       = (Number(Target[Key] ?? Minimum)) + Direction * Step * Multiplier;
            Finalize(Next);
            NumberInput.value = ((Number(Target[Key] ?? Minimum)) * Scale).toFixed(Digits);
        }
        else if (Event.key === "Enter")
        {
            Event.preventDefault();
            const Parsed = ParseTypedScalar();
            if (Number.isFinite(Parsed))
            {
                Finalize(Parsed);
            }
            if (NumberInput.blur)
            {
                NumberInput.blur();
            }
        }
        else if (Event.key === "Escape")
        {
            Event.preventDefault();
            Finalize(PriorBeforeEdit);
            if (NumberInput.blur)
            {
                NumberInput.blur();
            }
        }
    });

    // 📐 Horizontal scrub on the caption: 180 px of pointer travel spans [Minimum, Maximum] (Shift = 0.1× precision).
    let ScrubActive = false;
    let ScrubLastX  = 0.0;
    let ScrubFloat  = Initial;
    Caption.title = "Drag horizontally to scrub · Hold Shift for fine adjustment";
    Caption.addEventListener("pointerdown", (Event) =>
    {
        if (Event.button !== undefined && Event.button !== 0)
        {
            return;
        }
        ScrubActive = true;
        ScrubLastX  = Event.clientX || 0.0;
        ScrubFloat  = Number.isFinite(Number(Target[Key])) ? Number(Target[Key]) : Minimum;
        Root.classList.add("is-scrubbing");
        if (Caption.setPointerCapture && Event.pointerId !== undefined)
        {
            Caption.setPointerCapture(Event.pointerId);
        }
        Event.preventDefault();
    });
    Caption.addEventListener("pointermove", (Event) =>
    {
        if (!ScrubActive)
        {
            return;
        }
        const CurrentX = Event.clientX || 0.0;
        const DeltaX   = CurrentX - ScrubLastX;
        ScrubLastX     = CurrentX;
        const Gain     = Event.shiftKey ? 0.1 : 1.0;
        ScrubFloat     = Math.max(Minimum, Math.min(Maximum, ScrubFloat + (DeltaX / 180.0) * Span * Gain));
        Finalize(ScrubFloat);
    });
    const EndScrub = () =>
    {
        ScrubActive = false;
        Root.classList.remove("is-scrubbing");
    };
    Caption.addEventListener("pointerup", EndScrub);
    Caption.addEventListener("pointercancel", EndScrub);

    Refresh();
    return { Container: Root, Refresh };
}
