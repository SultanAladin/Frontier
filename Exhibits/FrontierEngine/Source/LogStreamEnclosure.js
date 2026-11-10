//============================================================================================================================================
//                                                         LOGSTREAMENCLOSURE.JS
//============================================================================================================================================
// 🧩 Docked Frontier Log Stream panel inside FrontierEngine — live GPU bring-up, ReSTIR/GDF, physics, vehicle, and drill diagnostics.

const RingCapacity = 600;

const ChannelAccents = Object.freeze({
    Render:       null,
    Illumination: "#2fe6a7",
    Distance:     "#5fd3bc",
    Physics:      "#7fb6ff",
    Vehicle:      "#9be3b5",
    Tyre:         "#ffb454",
    Training:     "#e6c665",
    Environment:  "#7ad9a8",
    Interface:    null,
    Foliage:      "#ff7ad9",
    Gameplay:     "#b18cff",
    Network:      "#63a8ff"
});

const SeverityAccents = Object.freeze({
    info:    "#2fe6a7",
    warning: "#ffb454",
    error:   "#ff6d6d",
    fatal:   "#ff4d6d"
});

const VerbosityRanks = Object.freeze({
    fatal:   0,
    error:   1,
    warning: 2,
    display: 3,
    log:     4,
    verbose: 5
});

function QueryById(Identifier)
{
    return typeof document !== "undefined" ? document.getElementById(Identifier) : null;
}

function MakeNode(Tag, ClassName = "", Text = "")
{
    const Created = document.createElement(Tag);
    if (ClassName)
        Created.className = ClassName;
    if (Text)
        Created.textContent = Text;
    return Created;
}

function FormatTimestamp(EpochMilliseconds)
{
    const Stamp = new Date(EpochMilliseconds);
    const Hours   = String(Stamp.getHours()).padStart(2, "0");
    const Minutes = String(Stamp.getMinutes()).padStart(2, "0");
    const Seconds = String(Stamp.getSeconds()).padStart(2, "0");
    const Millis  = String(Stamp.getMilliseconds()).padStart(3, "0");
    return `${Hours}:${Minutes}:${Seconds}.${Millis}`;
}

export class LogStreamEnclosure
{
    constructor(OnLayoutChanged = null)
    {
        this.OnLayoutChanged = OnLayoutChanged;
        this.Root            = QueryById("log-stream-enclosure");
        this.ToggleButton    = QueryById("log-stream-toggle");
        this.ViewportColumn  = QueryById("viewport-column");

        this.Visible         = false;
        this.Maximized       = false;
        this.Paused          = false;
        this.Following       = true;
        this.ActiveSeverity  = "all";
        this.ActiveChannel   = "all";
        this.ActiveVerbosity = "log";
        this.SearchQuery     = "";

        this.SessionStart    = Date.now();
        this.TotalRecorded   = 0;
        this.DroppedCount    = 0;
        this.PendingRate     = 0;
        this.PeakRate        = 0;
        this.RateBuckets     = new Array(56).fill(0);
        this.SeverityTallies = { all: 0, info: 0, warning: 0, error: 0, fatal: 0 };
        this.Entries         = [];
        this.Dirty           = false;
        this.HeartbeatTimer  = 0.0;

        if (this.Root && this.Root.appendChild && typeof document !== "undefined")
            this.ConstructDom();
        if (this.ToggleButton)
            this.ToggleButton.addEventListener("click", () => this.ToggleOpen());

        if (typeof setInterval !== "undefined" && this.Root)
        {
            this.Ticker = setInterval(() => this.AdvanceSecond(), 1000);
            if (this.Ticker && typeof this.Ticker.unref === "function")
                this.Ticker.unref();
        }
    }

    ConstructDom()
    {
        if (this.Root.replaceChildren)
            this.Root.replaceChildren();

        // ① Header
        const Head = MakeNode("header", "ls-head");
        const Brand = MakeNode("div", "ls-brand");
        Brand.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="#2fe6a7" stroke-width="2.4" stroke-linecap="round">`
                        + `<path d="M5 5.5v13M10.2 5.5v13M15.4 5.5v8M20.6 5.5v5"/></svg>`;
        const Title = MakeNode("h2", "ls-title", "Log Stream");
        this.LivePill = MakeNode("span", "ls-live-pill");
        this.LiveDot  = MakeNode("span", "ls-live-dot");
        this.LiveWord = MakeNode("span", "ls-live-word", "Live");
        this.LivePill.append(this.LiveDot, this.LiveWord);
        this.ClockNode = MakeNode("span", "ls-clock", FormatTimestamp(Date.now()));

        const Actions = MakeNode("div", "ls-actions");
        this.PauseBtn = this.ConstructIconButton("Pause / resume stream",
            `<svg viewBox="0 0 24 24" fill="currentColor"><rect x="6" y="5" width="4" height="14" rx="1.2"/><rect x="14" y="5" width="4" height="14" rx="1.2"/></svg>`,
            () => this.TogglePaused());
        const ClearBtn = this.ConstructIconButton("Clear stream",
            `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="M4 7h16M9.5 7V5h5v2M7 7l.8 11.5h8.4L17 7"/></svg>`,
            () => this.ClearEntries());
        const ExportBtn = this.ConstructIconButton("Export visible entries (.log)",
            `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="M12 4v10.5M7.8 10.8 12 15l4.2-4.2M5 19.5h14"/></svg>`,
            () => this.ExportVisibleLog());
        this.MaxBtn = this.ConstructIconButton("Maximize / dock Log Stream",
            `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="M8 4.5H4.5V8M16 4.5h3.5V8M8 19.5H4.5V16M16 19.5h3.5V16"/></svg>`,
            () => this.ToggleMaximized());
        const CloseBtn = this.ConstructIconButton("Hide Log Stream (`)",
            `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.1" stroke-linecap="round"><path d="M6 6l12 12M18 6L6 18"/></svg>`,
            () => this.ToggleOpen(false));
        Actions.append(this.PauseBtn, ClearBtn, ExportBtn, this.MaxBtn, CloseBtn);
        Head.append(Brand, Title, this.LivePill, this.ClockNode, MakeNode("div", "ls-spacer"), Actions);

        // ② Summary strip + sparkline
        const Summary = MakeNode("div", "ls-summary");
        const StatEntries = this.ConstructStatCell("Entries", "");
        this.NumEntries = StatEntries.NumberNode;
        this.RateBadge  = StatEntries.DeltaNode;

        const StatWarn = this.ConstructStatCell("Warnings", "amber");
        this.NumWarn  = StatWarn.NumberNode;
        this.WarnBadge = StatWarn.DeltaNode;
        this.WarnBadge.textContent = "0";

        const StatErr = this.ConstructStatCell("Errors", "coral");
        this.NumErr  = StatErr.NumberNode;
        this.ErrBadge = StatErr.DeltaNode;
        this.ErrBadge.textContent = "0";

        const SparkBox  = MakeNode("div", "ls-spark-box");
        const SparkHead = MakeNode("div", "ls-spark-head");
        SparkHead.appendChild(MakeNode("span", "", "Entries per second"));
        this.SparkMeta = MakeNode("span", "");
        this.SparkMeta.innerHTML = `<strong>0</strong> now · 0 peak`;
        SparkHead.appendChild(this.SparkMeta);
        this.SparkCanvas = MakeNode("canvas", "ls-spark-canvas");
        this.SparkCanvas.height = 28;
        SparkBox.append(SparkHead, this.SparkCanvas);
        Summary.append(StatEntries.Cell, StatWarn.Cell, StatErr.Cell, SparkBox);

        // ③ Filter row
        const Filters = MakeNode("div", "ls-filters");
        this.SeverityChips = {};
        for (const [Key, TitleText, DotColour] of [
            ["all", "All", null],
            ["info", "Info", SeverityAccents.info],
            ["warning", "Warnings", SeverityAccents.warning],
            ["error", "Errors", SeverityAccents.error],
            ["fatal", "Fatal", SeverityAccents.fatal]
        ])
        {
            const Chip = MakeNode("button", "ls-chip");
            Chip.type = "button";
            Chip.setAttribute("aria-pressed", Key === "all" ? "true" : "false");
            if (DotColour)
            {
                const Dot = MakeNode("span", "ls-chip-dot");
                Dot.style.background = DotColour;
                Chip.appendChild(Dot);
            }
            const CountSpan = MakeNode("span", "", "0");
            Chip.append(CountSpan, MakeNode("span", "", TitleText));
            Chip.addEventListener("click", () =>
            {
                this.ActiveSeverity = Key;
                for (const [OtherKey, EntryChip] of Object.entries(this.SeverityChips))
                    EntryChip.Button.setAttribute("aria-pressed", OtherKey === Key ? "true" : "false");
                this.RefreshRows();
            });
            this.SeverityChips[Key] = { Button: Chip, Count: CountSpan };
            Filters.appendChild(Chip);
        }

        this.ChannelSelect = MakeNode("select", "ls-select");
        const AllOpt = MakeNode("option", "", "All channels");
        AllOpt.value = "all";
        this.ChannelSelect.appendChild(AllOpt);
        for (const Name of Object.keys(ChannelAccents))
        {
            const Opt = MakeNode("option", "", Name);
            Opt.value = Name;
            this.ChannelSelect.appendChild(Opt);
        }
        this.ChannelSelect.addEventListener("change", () =>
        {
            this.ActiveChannel = this.ChannelSelect.value;
            this.RefreshRows();
        });

        this.VerbositySelect = MakeNode("select", "ls-select");
        for (const [Key, Label] of [["verbose", "Verbosity: Verbose"], ["log", "Verbosity: Log"], ["display", "Verbosity: Display"],
                                    ["warning", "Verbosity: Warning"], ["error", "Verbosity: Error"]])
        {
            const Opt = MakeNode("option", "", Label);
            Opt.value = Key;
            if (Key === "log")
                Opt.selected = true;
            this.VerbositySelect.appendChild(Opt);
        }
        this.VerbositySelect.addEventListener("change", () =>
        {
            this.ActiveVerbosity = this.VerbositySelect.value;
            this.RefreshRows();
        });

        const SearchWrap = MakeNode("label", "ls-search");
        this.SearchInput = MakeNode("input");
        this.SearchInput.type = "search";
        this.SearchInput.placeholder = "Filter text…";
        this.SearchInput.autocomplete = "off";
        this.SearchInput.spellcheck = false;
        this.SearchInput.addEventListener("input", () =>
        {
            this.SearchQuery = this.SearchInput.value.trim().toLowerCase();
            this.RefreshRows();
        });
        SearchWrap.appendChild(this.SearchInput);

        this.FollowBtn = MakeNode("button", "ls-follow-pill");
        this.FollowBtn.type = "button";
        this.FollowBtn.innerHTML = `<span class="ls-live-dot"></span><span>Following</span>`;
        this.FollowBtn.addEventListener("click", () =>
        {
            this.Following = !this.Following;
            this.FollowBtn.classList.toggle("off", !this.Following);
            if (this.Following)
                this.ScrollToNewest();
        });

        Filters.append(this.ChannelSelect, this.VerbositySelect, SearchWrap, MakeNode("div", "ls-spacer"), this.FollowBtn);

        // ④ Column labels + stream rows
        const Columns = MakeNode("div", "ls-columns");
        Columns.append(MakeNode("span"), MakeNode("span", "", "Time"), MakeNode("span", "", "Channel"),
                       MakeNode("span", "", "Message"), MakeNode("span", "", "Source"));

        this.RowsContainer = MakeNode("div", "ls-rows");
        this.RowsContainer.addEventListener("scroll", () =>
        {
            const Gap = this.RowsContainer.scrollHeight - this.RowsContainer.scrollTop - this.RowsContainer.clientHeight;
            if (Gap > 36 && this.Following)
            {
                this.Following = false;
                this.FollowBtn.classList.add("off");
            }
            else if (Gap <= 10 && !this.Following)
            {
                this.Following = true;
                this.FollowBtn.classList.remove("off");
            }
        });

        // ⑤ Footer
        const Foot = MakeNode("footer", "ls-foot");
        this.FootLeft  = MakeNode("span", "");
        this.FootRight = MakeNode("span", "");
        Foot.append(this.FootLeft, this.FootRight);

        this.Root.append(Head, Summary, Filters, Columns, this.RowsContainer, Foot);
        this.RefreshCounters();
        this.RefreshRows();
    }

    ConstructIconButton(TitleText, SvgMarkup, OnPress)
    {
        const Btn = MakeNode("button", "ls-icon-btn");
        Btn.type = "button";
        Btn.title = TitleText;
        Btn.innerHTML = SvgMarkup;
        Btn.addEventListener("click", OnPress);
        return Btn;
    }

    ConstructStatCell(LabelText, Tone)
    {
        const Cell = MakeNode("div", "ls-stat");
        const LabelNode = MakeNode("span", "ls-stat-label", LabelText);
        const Row = MakeNode("div", "ls-stat-row");
        const NumberNode = MakeNode("span", "ls-stat-num", "0");
        const DeltaNode = MakeNode("span", `ls-delta${Tone ? ` ${Tone}` : ""}`, "+0/s");
        Row.append(NumberNode, DeltaNode);
        Cell.append(LabelNode, Row);
        return { Cell, NumberNode, DeltaNode };
    }

    QueryOpen()
    {
        return !!this.Visible;
    }

    ToggleOpen(Force = undefined)
    {
        if (!this.Root)
            return;
        this.Visible = Force === undefined ? !this.Visible : !!Force;
        this.Root.classList.toggle("hidden", !this.Visible);
        if (!this.Visible && this.Maximized)
        {
            this.Maximized = false;
            if (this.ViewportColumn)
                this.ViewportColumn.classList.remove("log-stream-maximized");
            if (this.MaxBtn)
                this.MaxBtn.classList.remove("active");
        }
        if (this.ToggleButton)
            this.ToggleButton.classList.toggle("log-open", this.Visible);
        if (this.Visible)
        {
            this.RefreshCounters();
            this.RefreshRows();
            this.RenderSparkline();
        }
        if (this.OnLayoutChanged)
            this.OnLayoutChanged();
    }

    ToggleMaximized()
    {
        if (!this.Root)
            return;
        if (!this.Visible)
            this.ToggleOpen(true);
        this.Maximized = !this.Maximized;
        if (this.ViewportColumn)
            this.ViewportColumn.classList.toggle("log-stream-maximized", this.Maximized);
        if (this.MaxBtn)
            this.MaxBtn.classList.toggle("active", this.Maximized);
        if (this.OnLayoutChanged)
            this.OnLayoutChanged();
    }

    TogglePaused()
    {
        this.Paused = !this.Paused;
        if (this.LivePill)
            this.LivePill.classList.toggle("paused", this.Paused);
        if (this.LiveWord)
            this.LiveWord.textContent = this.Paused ? "Paused" : "Live";
        if (this.PauseBtn)
            this.PauseBtn.classList.toggle("active", this.Paused);
        if (!this.Paused)
            this.RefreshRows();
    }

    ClearEntries()
    {
        this.Entries.length = 0;
        this.TotalRecorded = 0;
        this.SeverityTallies = { all: 0, info: 0, warning: 0, error: 0, fatal: 0 };
        this.RefreshCounters();
        this.RefreshRows();
    }

    RecordDiagnostic(ChannelName, MessageText, SourceFile = "IlluminationHost.js", Severity = "info", Verbosity = "log")
    {
        const CleanSeverity = SeverityAccents[Severity] ? Severity : "info";
        const Now = Date.now();
        this.TotalRecorded++;
        this.PendingRate++;
        this.SeverityTallies.all++;
        this.SeverityTallies[CleanSeverity] = (this.SeverityTallies[CleanSeverity] || 0) + 1;

        const Previous = this.Entries.length > 0 ? this.Entries[this.Entries.length - 1] : null;
        if (Previous && Previous.Channel === ChannelName && Previous.Message === MessageText && Previous.Severity === CleanSeverity)
        {
            Previous.RepeatCount++;
            Previous.Timestamp = Now;
        }
        else
        {
            if (this.Entries.length >= RingCapacity)
            {
                this.Entries.shift();
                this.DroppedCount++;
            }
            this.Entries.push({
                Timestamp:   Now,
                Channel:     ChannelName,
                Message:     MessageText,
                Source:      SourceFile,
                Severity:    CleanSeverity,
                Verbosity:   VerbosityRanks[Verbosity] !== undefined ? Verbosity : "log",
                RepeatCount: 1
            });
        }

        if (this.Visible && !this.Paused)
        {
            this.RefreshCounters();
            this.RefreshRows();
        }
    }

    AdvanceTelemetry(Host, StepSeconds)
    {
        this.HeartbeatTimer += StepSeconds;
        if (this.HeartbeatTimer < 3.2)
            return;
        this.HeartbeatTimer = 0.0;
        if (!Host || !Host.Ready || !Host.Vehicle || !Host.Distance)
            return;

        const Fps = (1.0 / Math.max(Host.FrameSeconds || 0.016, 1.0e-4)).toFixed(0);
        const SpeedKmh = (Math.abs(Host.Vehicle.QueryTelemetry().Speed) * 3.6).toFixed(0);
        const Bricks = Host.Distance.FrameLevelCounts.reduce((Acc, Count) => Acc + Count, 0);
        this.RecordDiagnostic(
            "Illumination",
            `ReSTIR GI steady · ${Fps} fps · ceiling ${Host.Distance.CeilingHeight.toFixed(1)} m · GDF bricks ${Bricks} · ${Host.Cards.ScheduledTiles} card tiles`,
            "ResamplingSequence.js",
            "info",
            "verbose"
        );
        if (Number(SpeedKmh) > 8)
        {
            this.RecordDiagnostic(
                "Vehicle",
                `ControlVehicle ${SpeedKmh} km/h · gear ${Host.Vehicle.QueryTelemetry().Gear} · ${Host.Vehicle.QueryTelemetry().EngineRPM.toFixed(0)} rpm`,
                "VehicleSolver.js",
                "info",
                "log"
            );
        }
    }

    AdvanceSecond()
    {
        const Rate = this.PendingRate;
        this.PendingRate = 0;
        if (Rate > this.PeakRate)
            this.PeakRate = Rate;
        this.RateBuckets.push(Rate);
        if (this.RateBuckets.length > 56)
            this.RateBuckets.shift();

        if (!this.Visible || !this.Root)
            return;
        if (this.ClockNode)
            this.ClockNode.textContent = FormatTimestamp(Date.now());
        if (this.RateBadge)
            this.RateBadge.textContent = `+${Rate}/s`;
        if (this.SparkMeta)
            this.SparkMeta.innerHTML = `<strong>${Rate}</strong> now · ${this.PeakRate} peak`;
        this.RefreshCounters();
        this.RenderSparkline();
    }

    MatchesFilter(Entry)
    {
        if (this.ActiveSeverity !== "all" && Entry.Severity !== this.ActiveSeverity)
            return false;
        if (this.ActiveChannel !== "all" && Entry.Channel !== this.ActiveChannel)
            return false;
        const AllowedRank = VerbosityRanks[this.ActiveVerbosity] ?? 4;
        const EntryRank = VerbosityRanks[Entry.Verbosity] ?? 4;
        if (EntryRank > AllowedRank)
            return false;
        if (this.SearchQuery)
        {
            const Haystack = `${Entry.Channel} ${Entry.Message} ${Entry.Source}`.toLowerCase();
            if (!Haystack.includes(this.SearchQuery))
                return false;
        }
        return true;
    }

    RefreshCounters()
    {
        if (!this.NumEntries)
            return;
        this.NumEntries.textContent = String(this.SeverityTallies.all);
        this.NumWarn.textContent    = String(this.SeverityTallies.warning);
        this.NumErr.textContent     = String(this.SeverityTallies.error + this.SeverityTallies.fatal);
        this.WarnBadge.textContent  = `${this.SeverityTallies.warning} total`;
        this.ErrBadge.textContent   = `${this.SeverityTallies.error + this.SeverityTallies.fatal} total`;
        for (const [Key, ChipRecord] of Object.entries(this.SeverityChips))
            ChipRecord.Count.textContent = String(this.SeverityTallies[Key] || 0);
    }

    RefreshRows()
    {
        if (!this.RowsContainer || !this.RowsContainer.replaceChildren)
            return;
        this.RowsContainer.replaceChildren();
        const Filtered = this.Entries.filter((Entry) => this.MatchesFilter(Entry));

        if (Filtered.length === 0)
        {
            this.RowsContainer.appendChild(MakeNode("div", "ls-empty", "No diagnostic entries match the active filters."));
        }
        else
        {
            for (const Entry of Filtered)
            {
                const Row = MakeNode("div", `ls-row sev-${Entry.Severity}`);
                const Dot = MakeNode("span", "ls-sev-dot");
                Dot.style.background = SeverityAccents[Entry.Severity] || SeverityAccents.info;

                const TimeSpan = MakeNode("span", "ls-time", FormatTimestamp(Entry.Timestamp));
                const ChanSpan = MakeNode("span", "ls-channel", Entry.Channel);
                const Accent = ChannelAccents[Entry.Channel];
                if (Accent)
                {
                    ChanSpan.style.color = Accent;
                    ChanSpan.style.borderColor = `${Accent}55`;
                }

                const MsgSpan = MakeNode("span", "ls-msg", Entry.Message);
                const MetaSpan = MakeNode("span", "ls-meta");
                if (Entry.RepeatCount > 1)
                    MetaSpan.appendChild(MakeNode("span", "ls-repeat", `×${Entry.RepeatCount}`));
                MetaSpan.appendChild(MakeNode("span", "ls-source", Entry.Source));

                Row.title = "Click to copy log line";
                Row.addEventListener("click", () =>
                {
                    const Line = `${FormatTimestamp(Entry.Timestamp)} [${Entry.Channel}] ${Entry.Message} (${Entry.Source})`;
                    if (typeof navigator !== "undefined" && navigator.clipboard && navigator.clipboard.writeText)
                        navigator.clipboard.writeText(Line).catch(() => {});
                });

                Row.append(Dot, TimeSpan, ChanSpan, MsgSpan, MetaSpan);
                this.RowsContainer.appendChild(Row);
            }
        }

        const ElapsedSeconds = Math.max(0, Math.floor((Date.now() - this.SessionStart) / 1000));
        const Mins = String(Math.floor(ElapsedSeconds / 60)).padStart(2, "0");
        const Secs = String(ElapsedSeconds % 60).padStart(2, "0");
        if (this.FootLeft)
            this.FootLeft.innerHTML = `<strong>${Filtered.length}</strong> shown · <strong>${this.Entries.length}</strong> resident`;
        if (this.FootRight)
            this.FootRight.innerHTML = `Ring <strong>${RingCapacity}</strong> · Dropped <strong>${this.DroppedCount}</strong> · Session <strong>${Mins}:${Secs}</strong>`;

        if (this.Following)
            this.ScrollToNewest();
    }

    ScrollToNewest()
    {
        if (this.RowsContainer)
            this.RowsContainer.scrollTop = this.RowsContainer.scrollHeight;
    }

    RenderSparkline()
    {
        if (!this.SparkCanvas || !this.SparkCanvas.getContext)
            return;
        const Context = this.SparkCanvas.getContext("2d");
        if (!Context || typeof Context.clearRect !== "function")
            return;
        const Bounds = this.SparkCanvas.getBoundingClientRect ? this.SparkCanvas.getBoundingClientRect() : { width: 260, height: 28 };
        const Width  = Math.max(120, Math.floor(Bounds.width || 260));
        const Height = 28;
        if (this.SparkCanvas.width !== Width)
            this.SparkCanvas.width = Width;
        Context.clearRect(0, 0, Width, Height);

        const Ceiling = Math.max(4, ...this.RateBuckets);
        const StepX = Width / Math.max(1, this.RateBuckets.length - 1);

        Context.strokeStyle = "rgba(255, 255, 255, 0.06)";
        Context.beginPath();
        Context.moveTo(0, Height * 0.5);
        Context.lineTo(Width, Height * 0.5);
        Context.stroke();

        Context.beginPath();
        this.RateBuckets.forEach((Count, Index) =>
        {
            const X = Index * StepX;
            const Y = Height - 3 - ((Count / Ceiling) * (Height - 6));
            if (Index === 0)
                Context.moveTo(X, Y);
            else
                Context.lineTo(X, Y);
        });
        Context.strokeStyle = "#2fe6a7";
        Context.lineWidth = 1.4;
        Context.stroke();

        Context.lineTo(Width, Height);
        Context.lineTo(0, Height);
        Context.closePath();
        Context.fillStyle = "rgba(47, 230, 167, 0.12)";
        Context.fill();
    }

    ExportVisibleLog()
    {
        if (typeof Blob === "undefined" || typeof URL === "undefined")
            return;
        const Lines = this.Entries
            .filter((Entry) => this.MatchesFilter(Entry))
            .map((Entry) => `${FormatTimestamp(Entry.Timestamp)}\t${Entry.Severity.toUpperCase()}\t[${Entry.Channel}]\t${Entry.Message}\t(${Entry.Source})`);
        const Payload = new Blob([Lines.join("\n") + "\n"], { type: "text/plain;charset=utf-8" });
        const Download = MakeNode("a");
        Download.href = URL.createObjectURL(Payload);
        Download.download = "FrontierEngine-LogStream.log";
        Download.click();
        URL.revokeObjectURL(Download.href);
    }
}
