/*==========================================================================================================================================
                                                        INSPECTORCONTROLS.JS
==========================================================================================================================================*/
// 🧩 Reusable inspector controls: glyphs, slider / switch / segmented rows, tiles, cards, series charts, the curve editor, scrolling.
'use strict';

const InspectorControls = (() =>
{
    const SvgNamespace = 'http://www.w3.org/2000/svg';
    // 📝 Colour coding: one hue per quantity family, one tone per state. The mode decides what wears the family colour —
    //    the label, the unit, or nothing — while a value keeps its state tone in every mode.
    const ColourModes = ['labels', 'units', 'none'];
    let   ColourMode  = 'labels';

    function SetColourMode(Mode)
    {
        ColourMode = ColourModes.includes(Mode) ? Mode : 'labels';
        if (typeof document !== 'undefined' && document.body)
        {
            ColourModes.forEach(Candidate => document.body.classList.remove('colour-' + Candidate));
            document.body.classList.add('colour-' + ColourMode);
        }
        return ColourMode;
    }

    function ReadColourMode()
    {
        return ColourMode;
    }

    // 📝 Temperature moves along one scale: cold below the window, green inside it, then warm, hot and out of range.
    function TemperatureTone(Value, Low, High)
    {
        if (Value < Low - 8) return 'cold';
        if (Value <= High) return 'live';
        if (Value <= High + 12) return 'warm';
        if (Value <= High + 30) return 'hot';
        return 'danger';
    }

    const Refreshers   = new Set();
    // 📝 The inspector page and the curve editor are rebuilt in place. Controls register inside a named scope as well as
    //    globally, so a rebuild releases exactly the controls it replaces and nothing that came after them.
    const Scopes       = new Map();
    const ScopeStack   = [];

    //------------------------------------------------------------------------------------------------------------------------
    //                                                        GLYPHS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 24 × 24 stroke glyphs, drawn for this prototype; the editor's own icon atlas replaces them in C++.
    const GlyphPaths = {
        Monitor:    '<rect x="2.5" y="4" width="19" height="13" rx="2.5"/><path d="M8 21h8M12 17v4"/>',
        Zap:        '<path d="M13 2.5 4.5 14h6.5l-1 7.5L18.5 10H12z"/>',
        Sun:        '<circle cx="12" cy="12" r="4"/><path d="M12 2.5v2M12 19.5v2M4.6 4.6l1.4 1.4M18 18l1.4 1.4M2.5 12h2M19.5 12h2M4.6 19.4 6 18M18 6l1.4-1.4"/>',
        Layers:     '<path d="M12 3 3 8l9 5 9-5z"/><path d="M3 12.5l9 5 9-5"/><path d="M3 17l9 5 9-5"/>',
        Scan:       '<path d="M4 8V5.5A1.5 1.5 0 0 1 5.5 4H8M16 4h2.5A1.5 1.5 0 0 1 20 5.5V8M20 16v2.5a1.5 1.5 0 0 1-1.5 1.5H16M8 20H5.5A1.5 1.5 0 0 1 4 18.5V16"/><path d="M7.5 12h9"/>',
        Gauge:      '<path d="M3.6 17.5a9 9 0 1 1 16.8 0"/><path d="M12 13.5l4.2-4.2"/><circle cx="12" cy="13.5" r="1.2"/>',
        Aperture:   '<circle cx="12" cy="12" r="9"/><path d="M14.3 3.3 9.5 11.5M20.7 9.6H11.2M18.4 18.3l-4.8-8.2M9.7 20.7l4.8-8.2M3.3 14.4h9.5M5.6 5.7l4.8 8.2"/>',
        Activity:   '<path d="M3 12h4l3-8 4 16 3-8h4"/>',
        Chevron:    '<path d="M6 9l6 6 6-6"/>',
        Check:      '<path d="M5 12.5l4.2 4.2L19 7"/>',
        Alert:      '<path d="M12 3.5 2.5 20h19z"/><path d="M12 10v4.5M12 17.4v.2"/>',
        Eye:        '<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
        EyeOff:     '<path d="M3 3l18 18"/><path d="M10.6 5.1A10.4 10.4 0 0 1 12 5c6.4 0 10 7 10 7a17.6 17.6 0 0 1-2.6 3.5M6.2 6.3C3.6 8.1 2 12 2 12s3.6 7 10 7c1.7 0 3.2-.5 4.5-1.2"/>',
        Search:     '<circle cx="11" cy="11" r="7"/><path d="M20 20l-3.8-3.8"/>',
        Sliders:    '<path d="M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>',
        Cloud:      '<path d="M7 18.5h10a4 4 0 0 0 .6-7.95A6 6 0 0 0 6.1 9.6 4.5 4.5 0 0 0 7 18.5z"/>',
        Star:       '<path d="M12 3.2l2.6 5.6 6.1.7-4.5 4.2 1.2 6.1L12 16.8l-5.4 3 1.2-6.1-4.5-4.2 6.1-.7z"/>',
        Atmosphere: '<circle cx="12" cy="12" r="9"/><path d="M3.5 9.5c3 1.4 6.2 2 8.5 2s5.5-.6 8.5-2M3.6 14.8c2.9 1.2 5.8 1.8 8.4 1.8s5.5-.6 8.4-1.8"/>',
        Wind:       '<path d="M3 8.5h11a3 3 0 1 0-3-3M3 12.5h15.5a3 3 0 1 1-3 3M3 16.5h7"/>',
        Copy:       '<rect x="9" y="9" width="11.5" height="11.5" rx="2.5"/><path d="M5.5 15H5a1.5 1.5 0 0 1-1.5-1.5V5A1.5 1.5 0 0 1 5 3.5h8.5A1.5 1.5 0 0 1 15 5v.5"/>',
        Reset:      '<path d="M3.5 12a8.5 8.5 0 1 0 2.6-6.1"/><path d="M3.5 4.5v4.6h4.6"/>',
        Plus:       '<path d="M12 5v14M5 12h14"/>',
        Close:      '<path d="M6.5 6.5l11 11M17.5 6.5l-11 11"/>',
        Clock:      '<circle cx="12" cy="12" r="9"/><path d="M12 7.5V12l3 2"/>',
        Target:     '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4.5"/><circle cx="12" cy="12" r="0.6"/>',
        Flag:       '<path d="M5 21V4M5 4.5h10.5l-1.4 3.6 1.4 3.6H5"/>',
        Folder:     '<path d="M3 7.5A2.5 2.5 0 0 1 5.5 5h3.6l2 2.2h7.4A2.5 2.5 0 0 1 21 9.7v7.8a2.5 2.5 0 0 1-2.5 2.5h-13A2.5 2.5 0 0 1 3 17.5z"/>',
        Dot:        '<circle cx="12" cy="12" r="3"/>',
        // 📝 Vehicle-specific glyphs: chassis, wheel, temperature, mass, gear train, brake disc, road.
        Vehicle:    '<path d="M3 15.5 4.6 11l1.9-3.4h11l1.9 3.4L21 15.5v2.2a1 1 0 0 1-1 1h-1.4a1 1 0 0 1-1-1v-.7H6.4v.7a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z"/>'
                    + '<path d="M3.4 13.6h17.2"/><circle cx="7.2" cy="17.6" r="1.1"/><circle cx="16.8" cy="17.6" r="1.1"/>',
        Tyre:       '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="3.6"/>'
                    + '<path d="M12 3v2.2M12 18.8V21M3 12h2.2M18.8 12H21M5.6 5.6l1.6 1.6M16.8 16.8l1.6 1.6M5.6 18.4l1.6-1.6M16.8 7.2l1.6-1.6"/>',
        Thermometer:'<path d="M14 14.8V5.5a2 2 0 1 0-4 0v9.3a4 4 0 1 0 4 0z"/><path d="M12 10.5v6.4"/><circle cx="12" cy="18" r="1.4"/>',
        Weight:     '<path d="M4.6 20.2h14.8L17.6 8H6.4z"/><path d="M9.2 8V6.2a2.8 2.8 0 0 1 5.6 0V8"/>',
        GearTrain:  '<circle cx="12" cy="12" r="3.2"/><path d="M12 3.2v2.4M12 18.4v2.4M4.8 7.6l2 1.2M17.2 15.2l2 1.2M4.8 16.4l2-1.2M17.2 8.8l2-1.2"/>',
        Brake:      '<circle cx="12" cy="12" r="8.4"/><circle cx="12" cy="12" r="3"/><path d="M17.4 6.6a8.4 8.4 0 0 0-2.6-1.5l-1 2.4M7 7.4 4.6 8.9"/>',
        Road:       '<path d="M4 20 8 4M20 20 16 4M12 5v3M12 11v3M12 17v2"/>',
        Table:      '<rect x="3" y="4" width="18" height="16" rx="2.5"/><path d="M3 9.5h18M9 9.5V20"/>',
        Pause:      '<rect x="6.5" y="5" width="4" height="14" rx="1.2"/><rect x="13.5" y="5" width="4" height="14" rx="1.2"/>',
        Play:       '<path d="M7.5 4.8 19 12 7.5 19.2z"/>'
    };

    function ConstructGlyph(Name)
    {
        const Svg = document.createElementNS(SvgNamespace, 'svg');
        Svg.setAttribute('viewBox', '0 0 24 24');
        Svg.setAttribute('class', 'glyph');
        Svg.innerHTML = GlyphPaths[Name] || GlyphPaths.Dot;
        return Svg;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    ELEMENT ASSEMBLY
    //------------------------------------------------------------------------------------------------------------------------

    function Construct(Tag, ClassName, Content)
    {
        const Element = document.createElement(Tag);
        if (ClassName) Element.className = ClassName;
        if (Content === undefined || Content === null) return Element;
        if (typeof Content === 'string' || typeof Content === 'number') Element.textContent = String(Content);
        else if (Array.isArray(Content)) Content.forEach(Part => Part !== null && Part !== undefined && Element.append(Part));
        else Element.append(Content);
        return Element;
    }

    function ConstructSvg(Tag, Attributes, Text)
    {
        const Element = document.createElementNS(SvgNamespace, Tag);
        for (const [Key, Setting] of Object.entries(Attributes || {})) Element.setAttribute(Key, Setting);
        if (Text !== undefined) Element.textContent = Text;
        return Element;
    }

    // 📝 Every control registers one refresher; any write broadcasts to all of them, so readouts, charts and the outliner
    //    follow one source of truth, as InspectorRuntime revisions do in C++.
    function Register(Refresher)
    {
        Refreshers.add(Refresher);
        ScopeStack.forEach(Scope => Scope.add(Refresher));
        Refresher();
        return Refresher;
    }

    function OpenScope(Name)
    {
        if (!Scopes.has(Name)) Scopes.set(Name, new Set());
        ScopeStack.push(Scopes.get(Name));
    }

    function CloseScope()
    {
        ScopeStack.pop();
    }

    function ReleaseScope(Name)
    {
        const Scope = Scopes.get(Name);
        if (!Scope) return;
        Scope.forEach(Refresher => Refreshers.delete(Refresher));
        Scope.clear();
    }

    function Broadcast()
    {
        Refreshers.forEach(Refresher => Refresher());
    }

    function ResolveAvailability(Binding)
    {
        return Binding.Availability ? Binding.Availability() : { Enabled: true, Reason: '' };
    }

    // 📝 A big readout: the whole part bright, the decimals dimmed, the unit small (the dashboard reference's "78.3 %").
    function ConstructMetric(Element, Text, Unit)
    {
        Element.textContent = '';
        const Point = String(Text).indexOf('.');
        const Digits = String(Text);
        Element.append(document.createTextNode(Point < 0 ? Digits : Digits.slice(0, Point)));
        if (Point >= 0) Element.append(Construct('span', 'fraction', Digits.slice(Point)));
        if (Unit) Element.append(Construct('span', 'unit', Unit));
    }

    function ConstructTags(Tags)
    {
        return (Tags || []).map(([Text, Tone]) => Construct('span', 'tag ' + (Tone || 'muted'), Text));
    }

    function ConstructRowLabel(Binding)
    {
        return Construct('div', 'row-label', [
            Construct('span', null, Binding.Label), ...ConstructTags(Binding.Tags), Construct('span', 'spacer'),
            Construct('span', 'row-reason', '')
        ]);
    }

    function RefreshAvailability(Row, Binding)
    {
        const Availability = ResolveAvailability(Binding);
        Row.classList.toggle('disabled', !Availability.Enabled);
        const Reason = Row.querySelector('.row-reason');
        if (Reason) Reason.textContent = Availability.Enabled ? '' : Availability.Reason;
        return Availability.Enabled;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                      CONTROL ROWS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Binding: { Label, Read(), Write(Quantity), Minimum, Maximum, Step, Unit, Digits, Labels, Tags, Availability() }.
    //    The Frontier Editor's field: a numeric pill with its unit beside a pill track with a white knob. Quantities stay
    //    canonical; `Convert` maps them for display when the inspector is set to imperial units.
    // 📝 The owner's two references in one control: labels coloured, only the units coloured, or plain text.
    function ColourPill(Parent, Binding)
    {
        const Caption = Construct('span', 'caption', (Binding && Binding.Caption) || 'Text colour');
        const Row = Construct('div', 'colour-row', [Caption]);
        Parent.append(Row);
        PillTabs(Row, {
            Options: [['Labels', 'labels'], ['Units', 'units'], ['Plain', 'none']],
            Read: ReadColourMode,
            Write: Mode => SetColourMode(Mode)
        });
        return Row;
    }

    function SliderRow(Parent, Binding)
    {
        const Digits = Binding.Digits ?? 2;
        const Convert = Binding.Convert || (Quantity => Quantity);
        const Decode  = Binding.Decode || (Quantity => Quantity);
        const Bound = Quantity => (typeof Quantity === 'function' ? Quantity() : Quantity);
        // 📝 The bounds are resolved on every use, so switching the inspector to imperial units rescales the same track
        //    without rebuilding the control (a linear scale keeps every fraction; temperature stays affine in the pill).
        const Minimum   = () => Convert(Bound(Binding.Minimum));
        const Maximum   = () => Convert(Bound(Binding.Maximum));
        const StepValue = () => Math.abs(Convert(Bound(Binding.Step)) - Convert(0)) || 1e-3;
        const Row    = Construct('div', 'control-row');
        if (Binding.Quantity) Row.dataset.quantity = typeof Binding.Quantity === 'function' ? Binding.Quantity() : Binding.Quantity;
        const Input  = Construct('input');
        Input.type = 'text';
        Input.inputMode = 'decimal';
        Input.setAttribute('aria-label', Binding.Label);
        const UnitLabel = Binding.Unit ? Construct('span', 'unit', Binding.Unit()) : null;
        const Pill  = Construct('div', 'number-pill', [Input, UnitLabel]);
        const Fill  = Construct('div', 'fill');
        const Knob  = Construct('div', 'knob');
        const Track = Construct('div', 'track', [Fill]);
        Track.tabIndex = 0;
        Track.setAttribute('role', 'slider');
        Track.setAttribute('aria-label', Binding.Label);
        const StepCount = Math.round((Maximum() - Minimum()) / StepValue());
        if (StepCount <= 12)
        {
            for (let Notch = 1; Notch < StepCount; ++Notch)
            {
                const Mark = Construct('div', 'notch');
                Mark.dataset.share = String(Notch / StepCount);
                Track.append(Mark);
            }
        }
        Track.append(Knob);
        Row.append(ConstructRowLabel(Binding), Construct('div', 'slider-line', [Pill, Track]));
        if (Binding.Labels) Row.append(Construct('div', 'end-labels', [Construct('span', null, Binding.Labels[0]), Construct('span', null, Binding.Labels[1])]));
        Parent.append(Row);

        const Quantize = Quantity =>
        {
            const Raw = Decode(Quantity);
            const Low = Bound(Binding.Minimum), High = Bound(Binding.Maximum), Step = Bound(Binding.Step);
            const Snapped = Low + Math.round((Raw - Low) / Step) * Step;
            return Math.min(High, Math.max(Low, Number(Snapped.toFixed(6))));
        };
        const Share = Quantity => (Convert(Quantity) - Minimum()) / Math.max(1e-9, Maximum() - Minimum());
        const PlaceKnob = Fraction =>
        {
            // 📝 The knob centre travels inside the track's end caps (18 px each), so it never leaves the pill.
            const Width  = Track.clientWidth || 200;
            const Centre = 18 + Fraction * Math.max(0, Width - 36);
            Knob.style.left  = Centre + 'px';
            Fill.style.width = (Centre + 18) + 'px';
            Track.querySelectorAll('.notch').forEach(Mark => { Mark.style.left = (18 + Number(Mark.dataset.share) * (Width - 36)) + 'px'; });
        };
        const WriteQuantity = Quantity =>
        {
            const Snapped = Quantize(Quantity);
            if (Snapped !== Binding.Read()) { Binding.Write(Snapped); Broadcast(); }
        };
        const FractionAt = ClientX =>
        {
            const Bounds = Track.getBoundingClientRect();
            return Math.min(1, Math.max(0, (ClientX - Bounds.left - 18) / Math.max(1, Bounds.width - 36)));
        };
        Track.addEventListener('pointerdown', Event =>
        {
            Track.setPointerCapture(Event.pointerId);
            Track.classList.add('dragging');
            WriteQuantity(Minimum() + FractionAt(Event.clientX) * (Maximum() - Minimum()));
        });
        Track.addEventListener('pointermove', Event =>
        {
            if (!Track.classList.contains('dragging')) return;
            WriteQuantity(Minimum() + FractionAt(Event.clientX) * (Maximum() - Minimum()));
        });
        const Release = () => Track.classList.remove('dragging');
        Track.addEventListener('pointerup', Release);
        Track.addEventListener('pointercancel', Release);
        Track.addEventListener('keydown', Event =>
        {
            const Steps = { ArrowLeft: -1, ArrowDown: -1, ArrowRight: 1, ArrowUp: 1, PageDown: -10, PageUp: 10 }[Event.key];
            if (Steps !== undefined) { WriteQuantity(Convert(Binding.Read()) + Steps * StepValue()); Event.preventDefault(); }
            else if (Event.key === 'Home') { WriteQuantity(Minimum()); Event.preventDefault(); }
            else if (Event.key === 'End') { WriteQuantity(Maximum()); Event.preventDefault(); }
        });
        const CommitInput = () =>
        {
            const Parsed = parseFloat(Input.value.replace(',', '.'));
            if (Number.isFinite(Parsed)) WriteQuantity(Parsed);
            Input.value = Number(Convert(Binding.Read())).toFixed(Digits);
        };
        Input.addEventListener('change', CommitInput);
        Input.addEventListener('keydown', Event => { if (Event.key === 'Enter') Input.blur(); });
        new ResizeObserver(() => PlaceKnob(Share(Binding.Read()))).observe(Track);

        Register(() =>
        {
            RefreshAvailability(Row, Binding);
            if (Binding.Quantity) Row.dataset.quantity = typeof Binding.Quantity === 'function' ? Binding.Quantity() : Binding.Quantity;
            PlaceKnob(Share(Binding.Read()));
            if (UnitLabel) UnitLabel.textContent = Binding.Unit();
            Track.setAttribute('aria-valuenow', String(Number(Convert(Binding.Read())).toFixed(Digits)));
            if (document.activeElement !== Input) Input.value = Number(Convert(Binding.Read())).toFixed(Digits);
        });
        return Row;
    }

    // 📝 Binding: { Label, Detail, Read(), Write(Enabled), Tags, Availability() }.
    function SwitchRow(Parent, Binding)
    {
        const Row    = Construct('div', 'control-row');
        if (Binding.Quantity) Row.dataset.quantity = typeof Binding.Quantity === 'function' ? Binding.Quantity() : Binding.Quantity;
        const Switch = Construct('div', 'switch');
        Switch.setAttribute('role', 'switch');
        const Text = Construct('div', 'switch-text', [
            Construct('div', 'switch-label', [Construct('span', null, Binding.Label), ...ConstructTags(Binding.Tags)]),
            Binding.Detail ? Construct('div', 'switch-detail', Binding.Detail) : null,
            Construct('div', 'switch-detail row-reason', '')
        ]);
        const Line = Construct('div', 'switch-line', [Text, Switch]);
        Line.tabIndex = 0;
        Row.append(Line);
        Parent.append(Row);
        const Flip = () => { Binding.Write(!Binding.Read()); Broadcast(); };
        Line.addEventListener('click', Flip);
        Line.addEventListener('keydown', Event => { if (Event.key === ' ' || Event.key === 'Enter') { Flip(); Event.preventDefault(); } });
        Register(() =>
        {
            RefreshAvailability(Row, Binding);
            const Enabled = !!Binding.Read();
            Switch.classList.toggle('on', Enabled);
            Switch.setAttribute('aria-checked', String(Enabled));
        });
        return Row;
    }

    // 📝 Binding: { Label, Options: [[Text, Setting], …], Read(), Write(Setting), Tags, Availability() }.
    function SegmentedRow(Parent, Binding)
    {
        const Row     = Construct('div', 'control-row');
        const Group   = Construct('div', 'segmented');
        const Buttons = Binding.Options.map(([Text, Setting]) =>
        {
            const Button = Construct('button', null, Text);
            Button.type = 'button';
            Button.addEventListener('click', () => { Binding.Write(Setting); Broadcast(); });
            Group.append(Button);
            return [Button, Setting];
        });
        if (Binding.Label) Row.append(ConstructRowLabel(Binding));
        Row.append(Group);
        Parent.append(Row);
        Register(() =>
        {
            RefreshAvailability(Row, Binding);
            const Current = Binding.Read();
            Buttons.forEach(([Button, Setting]) => Button.classList.toggle('active', Setting === Current));
        });
        return Row;
    }

    // 📝 The dashboard reference's pill tabs: a raised pill marks the active choice.
    function PillTabs(Parent, Binding)
    {
        const Group   = Construct('div', 'pill-tabs');
        const Buttons = Binding.Options.map(([Text, Setting]) =>
        {
            const Button = Construct('button', 'pill-tab', Text);
            Button.type = 'button';
            Button.addEventListener('click', () => { Binding.Write(Setting); Broadcast(); });
            Group.append(Button);
            return [Button, Setting];
        });
        Parent.append(Group);
        Register(() =>
        {
            const Current = Binding.Read();
            Buttons.forEach(([Button, Setting]) => Button.classList.toggle('active', Setting === Current));
        });
        return Group;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                         TILES
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The Frontier Editor's property tile: icon chip, name, ON / OFF / UNAVAILABLE.
    function ToggleTile(Parent, Binding)
    {
        const Status = Construct('div', 'tile-status');
        const Tile   = Construct('div', 'toggle-tile', [
            Construct('div', 'tile-chip', ConstructGlyph(Binding.Glyph)), Construct('div', 'tile-name', Binding.Label), Status
        ]);
        Tile.tabIndex = 0;
        Parent.append(Tile);
        const Flip = () =>
        {
            if (!ResolveAvailability(Binding).Enabled) return;
            Binding.Write(!Binding.Read());
            Broadcast();
        };
        Tile.addEventListener('click', Flip);
        Tile.addEventListener('keydown', Event => { if (Event.key === ' ' || Event.key === 'Enter') { Flip(); Event.preventDefault(); } });
        Register(() =>
        {
            const Availability = ResolveAvailability(Binding);
            const Enabled      = Availability.Enabled && !!Binding.Read();
            Tile.classList.toggle('on', Enabled);
            Tile.classList.toggle('unavailable', !Availability.Enabled);
            Status.textContent = !Availability.Enabled ? 'UNAVAILABLE' : Enabled ? 'ON' : 'OFF';
            Tile.title = Availability.Enabled ? '' : Availability.Reason;
        });
        return Tile;
    }

    // 📝 Binding: { Label, Glyph, Resolve() → [Text, Unit, Tone] }. Tone colours the value, as the reference does.
    function ReadoutTile(Parent, Binding)
    {
        const Reading = Construct('div', 'tile-value');
        const Tile    = Construct('div', 'readout-tile', [
            Binding.Glyph ? Construct('div', 'tile-glyph', ConstructGlyph(Binding.Glyph)) : null, Reading,
            Construct('div', 'tile-label', Binding.Label)
        ]);
        if (Binding.Quantity) Tile.dataset.quantity = typeof Binding.Quantity === 'function' ? Binding.Quantity() : Binding.Quantity;
        if (Binding.onSelect) { Tile.tabIndex = 0; Tile.classList.add('selectable'); Tile.addEventListener('click', Binding.onSelect); }
        Parent.append(Tile);
        Register(() =>
        {
            const [Text, Unit, Tone] = Binding.Resolve();
            if (Binding.Quantity) Tile.dataset.quantity = typeof Binding.Quantity === 'function' ? Binding.Quantity() : Binding.Quantity;
            Reading.textContent = Text;
            if (Unit !== undefined && Unit !== null && Unit !== '') Reading.append(Construct('small', null, Unit));
            Reading.className = 'tile-value' + (Tone ? ' tone-' + Tone : '');
        });
        return Tile;
    }

    // 📝 A coloured instrument card: dot, name, big value with unit and a status line (tyre and thermal gauges).
    //    Resolve() may return a Name, because a tyre's name carries the compound it was just fitted with.
    function InstrumentCard(Parent, Binding)
    {
        const Dot     = Construct('span', 'instrument-dot');
        const Name    = Construct('span', 'instrument-name', Binding.Label);
        const Reading = Construct('div', 'instrument-value');
        const Status  = Construct('div', 'instrument-status');
        const Card    = Construct('div', 'instrument-card', [
            Construct('div', 'instrument-head', [Dot, Name]), Reading, Status
        ]);
        if (Binding.Quantity) Card.dataset.quantity = Binding.Quantity;
        Card.tabIndex = 0;
        if (Binding.onSelect) Card.addEventListener('click', Binding.onSelect);
        Parent.append(Card);
        Register(() =>
        {
            const InstrumentReading = Binding.Resolve();
            if (InstrumentReading.Name) Name.textContent = InstrumentReading.Name;
            Dot.style.background = InstrumentReading.Colour || '#787878';
            Card.classList.toggle('selected', !!InstrumentReading.Selected);
            Reading.textContent = InstrumentReading.Text;
            if (InstrumentReading.Unit) Reading.append(Construct('small', null, InstrumentReading.Unit));
            Reading.className = 'instrument-value' + (InstrumentReading.ValueTone ? ' tone-' + InstrumentReading.ValueTone : '');
            Status.textContent = InstrumentReading.Status;
            Status.className = 'instrument-status tone-' + (InstrumentReading.Tone || 'muted');
        });
        return Card;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     SECTION CARDS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Binding: { Id, Title, Glyph, Colour, Status() }. The card arrives unfolded; a section tab folds it.
    function SectionCard(Parent, Binding)
    {
        const Status = Construct('div', 'head-status');
        const Head   = Construct('div', 'section-head', [
            Construct('div', 'head-chip', ConstructGlyph(Binding.Glyph)), Construct('div', 'head-title', Binding.Title),
            Construct('div', 'spacer'), Status, Construct('div', 'chevron', ConstructGlyph('Chevron'))
        ]);
        if (Binding.Colour)
        {
            Head.querySelector('.head-chip').style.color = Binding.Colour;
            Head.querySelector('.head-chip').style.background = Binding.Colour + '1f';
        }
        const Inner = Construct('div', 'section-inner');
        const Card  = Construct('section', 'section-card', [Head, Construct('div', 'section-fold', Construct('div', 'section-body', Inner))]);
        Card.id = 'Section-' + Binding.Id;
        Head.addEventListener('click', () => Card.classList.toggle('folded'));
        Parent.append(Card);
        if (Binding.Status) Register(() => { Status.textContent = Binding.Status(); });
        return { Card, Inner, Status, Head };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                        CHARTS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The dashboard reference's chart scales: dashed guides, dim axis labels, a thin bright line with a soft area.
    function SeriesChart(Parent, Binding)
    {
        const Height  = Binding.Height || 150;
        const Svg     = ConstructSvg('svg', { class: 'series', height: Height });
        const Hover   = ConstructSvg('g', { class: 'hover-layer' });
        const Caption = Construct('div', 'chart-caption');
        Parent.append(Svg);
        if (Binding.Caption !== false) Parent.append(Caption);
        const Format = (Quantity, UnitKey, Digits) => VehicleCatalogue.FormatQuantity(Quantity, Digits, UnitKey,
            Binding.UnitSystem ? Binding.UnitSystem() : 'Metric');
        const Layout = () =>
        {
            const Width = Math.max(140, Svg.clientWidth || (Parent.clientWidth - 24) || 320);
            return { Width, Left: 34, Right: Math.max(140, Width - (Binding.Series.length > 1 ? 40 : 12)), Top: 10, Bottom: Height - 22 };
        };
        const AtX = (LayoutValue, Value) =>
        {
            const Axis = Binding.XAxis;
            return LayoutValue.Left + (Value - Axis.Minimum) / Math.max(1e-9, Axis.Maximum - Axis.Minimum) * (LayoutValue.Right - LayoutValue.Left);
        };
        const AtY = (LayoutValue, Axis, Value) =>
            LayoutValue.Bottom - (Value - Axis.Minimum) / Math.max(1e-9, Axis.Maximum - Axis.Minimum) * (LayoutValue.Bottom - LayoutValue.Top);
        const Refresh = () =>
        {
            const Box = Layout();
            Svg.textContent = '';
            Svg.setAttribute('viewBox', `0 0 ${Box.Width} ${Height}`);
            const PrimaryAxis = Binding.Series[0].Axis;
            // ① horizontal guides on the primary axis, labelled at the left
            const MajorStep = PrimaryAxis.MajorStep || (PrimaryAxis.Maximum - PrimaryAxis.Minimum) / 4;
            for (let Value = Math.ceil((PrimaryAxis.Minimum + 1e-9) / MajorStep) * MajorStep; Value <= PrimaryAxis.Maximum + 1e-6; Value += MajorStep)
            {
                const Y = AtY(Box, PrimaryAxis, Value);
                Svg.append(ConstructSvg('line', { x1: Box.Left, x2: Box.Right, y1: Y, y2: Y, stroke: '#ffffff', 'stroke-opacity': 0.06, 'stroke-dasharray': '2 5' }),
                           ConstructSvg('text', { x: Box.Left - 7, y: Y + 3, 'text-anchor': 'end' }, Format(Value, PrimaryAxis.Unit, 0).Text));
            }
            // ② the right axis belongs to the second series (power beside torque)
            if (Binding.Series.length > 1)
            {
                const SecondaryAxis = Binding.Series[1].Axis;
                const Step = SecondaryAxis.MajorStep || (SecondaryAxis.Maximum - SecondaryAxis.Minimum) / 4;
                for (let Value = Math.ceil((SecondaryAxis.Minimum + 1e-9) / Step) * Step; Value <= SecondaryAxis.Maximum + 1e-6; Value += Step)
                    Svg.append(ConstructSvg('text', { x: Box.Right + 7, y: AtY(Box, SecondaryAxis, Value) + 3 }, Format(Value, SecondaryAxis.Unit, 0).Text));
            }
            // ③ the X axis
            const XStep = Binding.XAxis.MajorStep || (Binding.XAxis.Maximum - Binding.XAxis.Minimum) / 4;
            for (let Value = Math.ceil((Binding.XAxis.Minimum + 1e-9) / XStep) * XStep; Value <= Binding.XAxis.Maximum + 1e-6; Value += XStep)
            {
                const X = AtX(Box, Value);
                Svg.append(ConstructSvg('text', { x: X, y: Height - 6, 'text-anchor': 'middle' }, Format(Value, Binding.XAxis.Unit, 0).Text));
            }
            // ④ each series: area, line and its end marker
            Binding.Series.forEach((Series, Index) =>
            {
                if (!Series.Points || Series.Points.length < 2) return;
                const Path = Series.Points.map(([X, Y], Order) =>
                    (Order ? 'L' : 'M') + AtX(Box, X).toFixed(1) + ' ' + AtY(Box, Series.Axis, Y).toFixed(1)).join(' ');
                if (Series.Area)
                {
                    const Gradient = ConstructSvg('linearGradient', { id: 'Area-' + (Series.Id || Index), x1: 0, y1: 0, x2: 0, y2: 1 });
                    Gradient.append(ConstructSvg('stop', { offset: '0%', 'stop-color': Series.Colour, 'stop-opacity': 0.16 }),
                                    ConstructSvg('stop', { offset: '100%', 'stop-color': Series.Colour, 'stop-opacity': 0 }));
                    const Defs = ConstructSvg('defs');
                    Defs.append(Gradient);
                    Svg.append(Defs);
                    Svg.append(ConstructSvg('path', { d: Path + ` L${AtX(Box, Series.Points[Series.Points.length - 1][0]).toFixed(1)} ${Box.Bottom}`
                        + ` L${AtX(Box, Series.Points[0][0]).toFixed(1)} ${Box.Bottom} Z`, fill: `url(#Area-${Series.Id || Index})` }));
                }
                Svg.append(ConstructSvg('path', { d: Path, fill: 'none', stroke: Series.Colour, 'stroke-width': Series.Width || 1.4,
                                                  'stroke-linejoin': 'round', 'stroke-dasharray': Series.Dash || '' }));
                const [EndX, EndY] = Series.Points[Series.Points.length - 1];
                Svg.append(ConstructSvg('circle', { cx: AtX(Box, EndX), cy: AtY(Box, Series.Axis, EndY), r: 2.6, fill: Series.Colour }));
            });
            // ⑤ a target the series is judged against, dashed and labelled, as the reference's efficiency target
            if (Binding.Target)
            {
                const TargetY = AtY(Box, PrimaryAxis, Binding.Target.Y);
                Svg.append(ConstructSvg('line', { x1: Box.Left, x2: Box.Right, y1: TargetY, y2: TargetY, stroke: '#ffffff', 'stroke-opacity': 0.5,
                                                  'stroke-dasharray': '7 6' }),
                           ConstructSvg('text', { x: Box.Left + 2, y: TargetY - 5, fill: '#9a9a9a' }, Binding.Target.Label));
            }
            // ⑥ authored point markers (the curve editor's own layer draws its handles instead)
            (Binding.Markers || []).forEach(Marker =>
            {
                const X = AtX(Box, Marker.X), Y = AtY(Box, PrimaryAxis, Marker.Y);
                Svg.append(ConstructSvg('circle', { cx: X, cy: Y, r: 3.2, fill: '#ffffff' }),
                           ConstructSvg('circle', { cx: X, cy: Y, r: 7, fill: '#ffffff', 'fill-opacity': 0.12 }));
                if (Marker.Label) Svg.append(ConstructSvg('text', { x: X, y: Y - 10, 'text-anchor': 'middle', fill: '#d8d8d8' }, Marker.Label));
            });
            // ⑦ axis labels: the left axis belongs to the first series, the right axis to the second when there is one
            Svg.append(ConstructSvg('text', { x: Box.Left, y: 8, fill: '#7a7a7a' },
                `${PrimaryAxis.Label} · ${Format(0, PrimaryAxis.Unit, 0).Unit}`),
                ConstructSvg('text', { x: Box.Right, y: 8, 'text-anchor': 'end', fill: '#7a7a7a' },
                    Binding.Series.length > 1 ? `${Binding.Series[1].Axis.Label} · ${Format(0, Binding.Series[1].Axis.Unit, 0).Unit}` : ''),
                ConstructSvg('text', { x: (Box.Left + Box.Right) / 2, y: 8, 'text-anchor': 'middle', fill: '#5e5e5e' },
                    `${Binding.XAxis.Label} · ${Format(0, Binding.XAxis.Unit, 0).Unit}`));
            Svg.append(Hover);
            if (Caption.textContent === '' && Binding.CaptionText) Caption.textContent = Binding.CaptionText;
        };
        new ResizeObserver(Refresh).observe(Svg);
        Register(Refresh);
        return { Svg, Refresh };
    }

    // 📝 The editable curve: authored control points the owner drags, adds, removes and nudges, with the interpolated line
    //    drawn through them. `Selection` is shared with the page so the numeric pills edit the same point.
    //    Binding: { Profile, Height, Selection: { Read(), Write(Index) }, OnEdit(), UnitSystem(), Target, ReadOnlyNotice }
    function CurveEditor(Parent, Binding)
    {
        const Profile = Binding.Profile;
        const Height  = Binding.Height || 220;
        const Editable = Profile.Editable !== false;
        const Svg     = ConstructSvg('svg', { class: 'curve', height: Height });
        const Panel   = Construct('div', 'curve-panel' + (Editable ? '' : ' read-only'));
        const Head    = Construct('div', 'curve-head');
        const Hover   = Construct('div', 'curve-hover');
        Head.append(Construct('span', 'curve-head-title', Profile.Title), Construct('span', 'spacer'), Hover);
        Panel.append(Head, Svg);
        Parent.append(Panel);
        Svg.tabIndex = 0;
        Svg.setAttribute('role', 'application');
        Svg.setAttribute('aria-label', `${Profile.Title} curve editor`);
        if (Binding.Height) Svg.setAttribute('height', String(Height));
        const Format = (Quantity, UnitKey, Digits) => VehicleCatalogue.FormatQuantity(Quantity, Digits, UnitKey,
            Binding.UnitSystem ? Binding.UnitSystem() : 'Metric');
        const UnitSymbol = UnitKey => Format(0, UnitKey, 0).Unit;
        let HoverX = null;

        const Layout = () =>
        {
            const Width = Math.max(160, Svg.clientWidth || (Parent.clientWidth - 24) || 320);
            return { Width, Left: 38, Right: Math.max(150, Width - (Profile.DerivedFrom ? 40 : 14)), Top: 14, Bottom: Height - 24 };
        };
        const AtX = (Box, Value) => Box.Left + (Value - Profile.X.Minimum) / Math.max(1e-9, Profile.X.Maximum - Profile.X.Minimum) * (Box.Right - Box.Left);
        const AtY = (Box, Value) => Box.Bottom - (Value - Profile.Y.Minimum) / Math.max(1e-9, Profile.Y.Maximum - Profile.Y.Minimum) * (Box.Bottom - Box.Top);
        const FromX = (Box, Pixel) => Profile.X.Minimum + Math.min(1, Math.max(0, (Pixel - Box.Left) / Math.max(1, Box.Right - Box.Left)))
            * (Profile.X.Maximum - Profile.X.Minimum);
        const FromY = (Box, Pixel) => Profile.Y.Minimum + Math.min(1, Math.max(0, (Box.Bottom - Pixel) / Math.max(1, Box.Bottom - Box.Top)))
            * (Profile.Y.Maximum - Profile.Y.Minimum);
        const Points = () => VehicleCatalogue.ResolveCurvePoints(Profile, Binding.Specification, Binding.Profiles);
        const SelectedIndex = () => (Binding.Selection ? Binding.Selection.Read() : -1);
        const WriteSelection = Index => { if (Binding.Selection) { Binding.Selection.Write(Index); Broadcast(); } };

        function RasterizeHandles(Box)
        {
            if (!Editable) return;
            const Authored = Profile.Points;
            Authored.forEach((Point, Index) =>
            {
                const X = AtX(Box, Point[0]), Y = AtY(Box, Point[1]);
                const Selected = Index === SelectedIndex();
                Svg.append(ConstructSvg('circle', { cx: X, cy: Y, r: Selected ? 7.5 : 4.6, fill: Selected ? 'rgba(159,180,214,0.22)' : 'rgba(255,255,255,0.10)',
                                                  stroke: Selected ? '#dbe5f4' : 'none', 'stroke-width': 1.4 }));
                Svg.append(ConstructSvg('circle', { cx: X, cy: Y, r: Selected ? 4.2 : 3.3, fill: Selected ? '#ffffff' : '#e2e2e2',
                                                  class: 'curve-handle', 'data-index': String(Index) }));
            });
        }

        function RasterizeHover(Box)
        {
            if (HoverX === null) return;
            const Value = FromX(Box, HoverX);
            const X = AtX(Box, Value);
            Svg.append(ConstructSvg('line', { x1: X, x2: X, y1: Box.Top, y2: Box.Bottom, stroke: '#ffffff', 'stroke-opacity': 0.16 }));
            const Evaluated = VehicleCatalogue.EvaluateCurve(Profile, Value, Binding.Specification, Binding.Profiles);
            if (Evaluated >= Profile.Y.Minimum && Evaluated <= Profile.Y.Maximum)
                Svg.append(ConstructSvg('circle', { cx: X, cy: AtY(Box, Evaluated), r: 3.4, fill: '#ffffff' }));
            Hover.textContent = `${Format(Value, Profile.X.Unit, 0).Text} ${UnitSymbol(Profile.X.Unit)} · `
                + `${Format(Evaluated, Profile.Y.Unit, 1).Text} ${UnitSymbol(Profile.Y.Unit)}`;
        }

        const Refresh = () =>
        {
            const Box = Layout();
            Svg.textContent = '';
            Svg.setAttribute('viewBox', `0 0 ${Box.Width} ${Height}`);
            // ① guides and axis labels in display units
            const YStep = Profile.Y.MajorStep || (Profile.Y.Maximum - Profile.Y.Minimum) / 4;
            for (let Value = Math.ceil((Profile.Y.Minimum + 1e-9) / YStep) * YStep; Value <= Profile.Y.Maximum + 1e-6; Value += YStep)
            {
                const Y = AtY(Box, Value);
                Svg.append(ConstructSvg('line', { x1: Box.Left, x2: Box.Right, y1: Y, y2: Y, stroke: '#ffffff', 'stroke-opacity': 0.06, 'stroke-dasharray': '2 5' }),
                           ConstructSvg('text', { x: Box.Left - 7, y: Y + 3, 'text-anchor': 'end' }, Format(Value, Profile.Y.Unit, 0).Text));
            }
            const XStep = Profile.X.MajorStep || (Profile.X.Maximum - Profile.X.Minimum) / 4;
            for (let Value = Math.ceil((Profile.X.Minimum + 1e-9) / XStep) * XStep; Value <= Profile.X.Maximum + 1e-6; Value += XStep)
            {
                const X = AtX(Box, Value);
                Svg.append(ConstructSvg('line', { x1: X, x2: X, y1: Box.Top, y2: Box.Bottom, stroke: '#ffffff', 'stroke-opacity': 0.035, 'stroke-dasharray': '2 7' }),
                           ConstructSvg('text', { x: X, y: Height - 7, 'text-anchor': 'middle' }, Format(Value, Profile.X.Unit, 0).Text));
            }
            // ② the interpolated line, sampled through the same evaluator the readouts use
            const Samples = 160;
            const Line = [];
            for (let Index = 0; Index <= Samples; ++Index)
            {
                const Value = Profile.X.Minimum + (Profile.X.Maximum - Profile.X.Minimum) * Index / Samples;
                Line.push([Value, VehicleCatalogue.EvaluateCurve(Profile, Value, Binding.Specification, Binding.Profiles)]);
            }
            const Path = Line.map(([X, Y], Order) => (Order ? 'L' : 'M') + AtX(Box, X).toFixed(1) + ' ' + AtY(Box, Y).toFixed(1)).join(' ');
            const Gradient = ConstructSvg('linearGradient', { id: 'CurveArea', x1: 0, y1: 0, x2: 0, y2: 1 });
            Gradient.append(ConstructSvg('stop', { offset: '0%', 'stop-color': Profile.Colour, 'stop-opacity': 0.22 }),
                            ConstructSvg('stop', { offset: '100%', 'stop-color': Profile.Colour, 'stop-opacity': 0 }));
            const Defs = ConstructSvg('defs');
            Defs.append(Gradient);
            Svg.append(Defs);
            Svg.append(ConstructSvg('path', { d: Path + ` L${AtX(Box, Profile.X.Maximum).toFixed(1)} ${Box.Bottom} L${AtX(Box, Profile.X.Minimum).toFixed(1)} ${Box.Bottom} Z`,
                                              fill: 'url(#CurveArea)' }),
                       ConstructSvg('path', { d: Path, fill: 'none', stroke: Profile.Colour, 'stroke-width': 1.7, 'stroke-linejoin': 'round',
                                              'stroke-dasharray': Editable ? '' : '6 4' }));
            // ③ a rated limit or target, dashed
            if (Binding.Target && Binding.Target.Y >= Profile.Y.Minimum && Binding.Target.Y <= Profile.Y.Maximum)
            {
                const Y = AtY(Box, Binding.Target.Y);
                Svg.append(ConstructSvg('line', { x1: Box.Left, x2: Box.Right, y1: Y, y2: Y, stroke: '#ffffff', 'stroke-opacity': 0.5, 'stroke-dasharray': '7 6' }),
                           ConstructSvg('text', { x: Box.Left + 2, y: Y - 5, fill: '#9a9a9a' }, Binding.Target.Label));
            }
            RasterizeHandles(Box);
            RasterizeHover(Box);
            Svg.append(ConstructSvg('text', { x: Box.Left, y: 9, fill: '#5a5a5a' },
                `${Profile.Y.Label} · ${UnitSymbol(Profile.Y.Unit)}`),
                ConstructSvg('text', { x: Box.Right, y: 9, 'text-anchor': 'end', fill: '#5a5a5a' },
                    `${Profile.X.Label} · ${UnitSymbol(Profile.X.Unit)}`));
            if (!Editable) Svg.append(ConstructSvg('text', { x: Box.Right, y: Box.Top + 12, 'text-anchor': 'end', fill: '#6a6a6a' }, 'Derived'));
        };

        // 📝 Hit-testing runs against the rendered handles, so a 7 px finger target is what the owner actually touches.
        function ResolvePointAt(Event)
        {
            const Box = Layout();
            const Bounds = Svg.getBoundingClientRect();
            const PixelX = Event.clientX - Bounds.left, PixelY = Event.clientY - Bounds.top;
            let Nearest = -1, NearestDistance = 15;
            Profile.Points.forEach((Point, Index) =>
            {
                const Distance = Math.hypot(AtX(Box, Point[0]) - PixelX, AtY(Box, Point[1]) - PixelY);
                if (Distance < NearestDistance) { Nearest = Index; NearestDistance = Distance; }
            });
            return { Index: Nearest, Box, PixelX, PixelY, Bounds };
        }

        let Dragging = -1;
        Svg.addEventListener('pointermove', Event =>
        {
            const Box = Layout();
            const Bounds = Svg.getBoundingClientRect();
            HoverX = Event.clientX - Bounds.left;
            if (Dragging >= 0)
            {
                VehicleCatalogue.RelocateCurvePoint(Profile, Dragging, FromX(Box, HoverX), FromY(Box, Event.clientY - Bounds.top));
                if (Binding.OnEdit) Binding.OnEdit('relocate', Dragging);
            }
            Refresh();
        });
        Svg.addEventListener('pointerleave', () => { HoverX = null; if (Dragging < 0) Refresh(); });
        Svg.addEventListener('pointerdown', Event =>
        {
            if (!Editable) return;
            const Hit = ResolvePointAt(Event);
            if (Hit.Index >= 0)
            {
                Dragging = Hit.Index;
                WriteSelection(Hit.Index);
                Svg.setPointerCapture(Event.pointerId);
                Svg.focus();
                Event.preventDefault();
                return;
            }
            // 📝 Shift-click (or a double-click) on empty space authors a point where the owner pointed.
            if (Event.shiftKey) AddPointAt(Hit.Box, Hit.PixelX, Hit.PixelY);
        });
        const Release = () => { if (Dragging >= 0) { Dragging = -1; Broadcast(); } };
        Svg.addEventListener('pointerup', Release);
        Svg.addEventListener('pointercancel', Release);
        Svg.addEventListener('dblclick', Event =>
        {
            if (!Editable) return;
            const Box = Layout(); const Bounds = Svg.getBoundingClientRect();
            const Hit = ResolvePointAt(Event);
            if (Hit.Index < 0) AddPointAt(Box, Event.clientX - Bounds.left, Event.clientY - Bounds.top);
        });
        Svg.addEventListener('contextmenu', Event =>
        {
            if (!Editable) return;
            Event.preventDefault();
            const Hit = ResolvePointAt(Event);
            if (Hit.Index < 0) return;
            if (VehicleCatalogue.RemoveCurvePoint(Profile, Hit.Index))
            {
                Binding.Selection ? Binding.Selection.Write(Math.min(Hit.Index, Profile.Points.length - 1)) : null;
                if (Binding.OnEdit) Binding.OnEdit('remove', Hit.Index);
                Broadcast();
            }
        });
        Svg.addEventListener('keydown', Event =>
        {
            if (!Editable) return;
            const Index = SelectedIndex();
            if (Index < 0) return;
            const Box = Layout();
            const StepX = (Profile.X.Maximum - Profile.X.Minimum) / (Event.shiftKey ? 20 : 200);
            const StepY = (Profile.Y.Maximum - Profile.Y.Minimum) / (Event.shiftKey ? 20 : 200);
            const Point = Profile.Points[Index];
            const Moves = { ArrowLeft: [-StepX, 0], ArrowRight: [StepX, 0], ArrowUp: [0, StepY], ArrowDown: [0, -StepY] };
            if (Moves[Event.key])
            {
                VehicleCatalogue.RelocateCurvePoint(Profile, Index, Point[0] + Moves[Event.key][0], Point[1] + Moves[Event.key][1]);
                if (Binding.OnEdit) Binding.OnEdit('relocate', Index);
                // 📝 A nudge broadcasts, so the numeric pills and every derived readout move with the point.
                Broadcast();
                Event.preventDefault();
            }
            else if (Event.key === 'Delete' || Event.key === 'Backspace')
            {
                if (VehicleCatalogue.RemoveCurvePoint(Profile, Index))
                {
                    Binding.Selection ? Binding.Selection.Write(Math.min(Index, Profile.Points.length - 1)) : null;
                    if (Binding.OnEdit) Binding.OnEdit('remove', Index);
                    Broadcast();
                    Event.preventDefault();
                }
            }
        });

        function AddPointAt(Box, PixelX, PixelY)
        {
            const Index = VehicleCatalogue.InsertCurvePoint(Profile, FromX(Box, PixelX), FromY(Box, PixelY));
            if (Index < 0) { Toast('Too close to a neighbouring point'); return; }
            Binding.Selection ? Binding.Selection.Write(Index) : null;
            if (Binding.OnEdit) Binding.OnEdit('insert', Index);
            Broadcast();
        }

        new ResizeObserver(Refresh).observe(Svg);
        Register(Refresh);
        return { Refresh, ResolvePointAt };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    INERTIAL SCROLLING
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The editor panels' scrolling (no visible scrollbar): the wheel moves a target, the view eases toward it, and past
    //    either end the content stretches a little and springs back once the wheel rests.
    function SmoothScroll(Scroller, Content)
    {
        const Elasticity = 90;   // [px] the farthest the target may run past an end
        let Target = Scroller.scrollTop, Current = Target, Running = false, LastWheel = 0;
        const Limit = () => Math.max(0, Scroller.scrollHeight - Scroller.clientHeight);
        const Advance = Now =>
        {
            const Maximum = Limit();
            Target = Now - LastWheel > 140 ? Math.min(Math.max(Target, 0), Maximum) : Math.min(Math.max(Target, -Elasticity), Maximum + Elasticity);
            Current += (Target - Current) * 0.17;
            if (Math.abs(Target - Current) < 0.25) Current = Target;
            const Clamped = Math.min(Math.max(Current, 0), Maximum);
            Scroller.scrollTop = Clamped;
            const Stretch = Current - Clamped;
            Content.style.transform = Stretch ? `translateY(${(-Stretch * 0.45).toFixed(2)}px)` : '';
            if (Current !== Target || Stretch !== 0) requestAnimationFrame(Advance);
            else Running = false;
        };
        const Start = () => { if (!Running) { Running = true; requestAnimationFrame(Advance); } };
        Scroller.addEventListener('wheel', Event =>
        {
            if (Event.ctrlKey || Math.abs(Event.deltaX) > Math.abs(Event.deltaY)) return;
            Event.preventDefault();
            if (!Running) Current = Target = Scroller.scrollTop;
            Target += Event.deltaMode === 1 ? Event.deltaY * 18 : Event.deltaY;
            LastWheel = performance.now();
            Start();
        }, { passive: false });
        Scroller.addEventListener('scroll', () => { if (!Running) Current = Target = Scroller.scrollTop; });
        return {
            ScrollTo(Position)
            {
                if (!Running) Current = Scroller.scrollTop;
                Target = Math.min(Math.max(Position, 0), Limit());
                LastWheel = 0;
                Start();
            }
        };
    }

    function Toast(Text)
    {
        let Element = document.querySelector('.toast');
        if (!Element) { Element = Construct('div', 'toast'); document.body.append(Element); }
        Element.textContent = Text;
        Element.classList.add('shown');
        clearTimeout(Toast.Timer);
        Toast.Timer = setTimeout(() => Element.classList.remove('shown'), 1800);
    }

    return {
        GlyphPaths, ConstructGlyph, Construct, ConstructSvg, Register, Broadcast, OpenScope, CloseScope, ReleaseScope,
        ColourPill, SetColourMode, ReadColourMode, TemperatureTone,
        ConstructMetric, ConstructTags,
        SliderRow, SwitchRow, SegmentedRow, PillTabs, ToggleTile, ReadoutTile, InstrumentCard, SectionCard,
        SeriesChart, CurveEditor, SmoothScroll, Toast
    };
})();
