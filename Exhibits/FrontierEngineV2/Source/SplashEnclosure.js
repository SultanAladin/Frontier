//============================================================================================================================================
//                                                           SPLASHENCLOSURE.JS
//============================================================================================================================================
// 🧩 Startup loading splash screen and level selector (Proving Grounds · Lobby / Garage) over the FrontierEngine viewport.

// 📝 Boots immediately while WebGPU device negotiation, archive streaming, distance-volume transfer, card allocation, and
//    pipeline compilation progress in the background. Supports:
//    • Startup mode (`/`): user picks either level while watching live bring-up progress, then clicks Enter (or presses Enter).
//    • Direct switch mode (`?level=...&autostart=1`, triggered from the in-editor level dropdown): displays the loading splash
//      progress for the newly chosen level and automatically transitions into the viewport as soon as bring-up completes.
//    • Headless / capture skip (`?lobby=0` or `?capture=1`): stays hidden so automated harnesses run without synthetic clicks.

import { LevelAtlas, QueryLevelEntry, ResolveLevelKey, FormatLevelSearch } from "./LevelAtlas.js";

const FadeOutMilliseconds = 380;

function QueryNode(Identifier)
{
    return typeof document !== "undefined" ? document.getElementById(Identifier) : null;
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

const BlueprintMarkup = Object.freeze({
    proving: `
        <defs>
            <pattern id="splash-grid-proving" width="20" height="20" patternUnits="userSpaceOnUse">
                <path d="M 20 0 L 0 0 0 20" fill="none" stroke="#222323" stroke-width="1"/>
            </pattern>
        </defs>
        <rect x="0" y="0" width="340" height="250" fill="url(#splash-grid-proving)"/>
        <rect x="24" y="28" width="292" height="194" rx="10" fill="none" stroke="#3b3c3c" stroke-width="2"/>
        <path d="M 52 182 L 236 182" stroke="#2d2e2e" stroke-width="24" stroke-linecap="round" fill="none"/>
        <path d="M 236 182 C 298 182 298 86 232 86 L 72 86" stroke="#292a2a" stroke-width="20" stroke-linecap="round" fill="none"/>
        <circle cx="88" cy="140" r="28" fill="none" stroke="#323333" stroke-width="14"/>
        <circle cx="88" cy="140" r="28" fill="none" stroke="#525450" stroke-width="1" stroke-dasharray="4 5"/>
        <path d="M 148 170 L 176 170 L 176 154 Z" fill="#424440"/>
        <g fill="none" stroke="#4a4c48" stroke-width="1.8">
            <rect x="194" y="126" width="14" height="10"/>
            <rect x="214" y="122" width="14" height="10"/>
            <rect x="234" y="126" width="14" height="10"/>
        </g>
        <path d="M 58 72 L 112 72" stroke="#4c4e4e" stroke-width="11"/>
        <g fill="#686a66">
            <circle cx="132" cy="86" r="2.6"/><circle cx="156" cy="80" r="2.6"/>
            <circle cx="180" cy="86" r="2.6"/><circle cx="204" cy="80" r="2.6"/>
        </g>
        <path class="splash-trace-line" d="M 60 186 C 148 186 236 190 258 168 C 284 142 258 110 216 102 C 168 93 96 98 68 82"
              fill="none" stroke="#cfd2c8" stroke-width="1.6" stroke-dasharray="7 7" stroke-linecap="round" opacity="0.6"/>
        <circle cx="60" cy="186" r="3.8" fill="#3ec46d"/>
    `,
    lobby: `
        <defs>
            <pattern id="splash-grid-lobby" width="20" height="20" patternUnits="userSpaceOnUse">
                <path d="M 20 0 L 0 0 0 20" fill="none" stroke="#222323" stroke-width="1"/>
            </pattern>
        </defs>
        <rect x="0" y="0" width="340" height="250" fill="url(#splash-grid-lobby)"/>
        <circle cx="170" cy="125" r="98" fill="#141716" stroke="#424744" stroke-width="2.2"/>
        <circle cx="170" cy="125" r="84" fill="none" stroke="#2c312e" stroke-width="1.2" stroke-dasharray="5 5"/>
        <rect x="112" y="98" width="116" height="54" rx="5" fill="none" stroke="#9be3b5" stroke-width="1.4" stroke-dasharray="4 3" opacity="0.65"/>
        <circle cx="170" cy="125" r="24" fill="#1c2220" stroke="#7fd8aa" stroke-width="1.8"/>
        <circle cx="116" cy="74" r="19" fill="#181d1b" stroke="#5b6560" stroke-width="1.5"/>
        <circle cx="224" cy="74" r="19" fill="#181d1b" stroke="#5b6560" stroke-width="1.5"/>
        <circle cx="116" cy="176" r="19" fill="#181d1b" stroke="#5b6560" stroke-width="1.5"/>
        <circle cx="224" cy="176" r="19" fill="#181d1b" stroke="#5b6560" stroke-width="1.5"/>
        <path class="splash-trace-line" d="M 78 44 L 116 74 M 262 44 L 224 74 M 78 206 L 116 176 M 262 206 L 224 176"
              fill="none" stroke="#cfd2c8" stroke-width="1.5" stroke-dasharray="5 5" stroke-linecap="round" opacity="0.65"/>
        <circle cx="170" cy="125" r="4.2" fill="#3ec46d"/>
        <circle cx="116" cy="74" r="3" fill="#63a8ff"/><circle cx="224" cy="74" r="3" fill="#63a8ff"/>
        <circle cx="116" cy="176" r="3" fill="#ffb454"/><circle cx="224" cy="176" r="3" fill="#63a8ff"/>
    `
});

export class SplashEnclosure
{
    constructor(Host)
    {
        this.Host         = Host;
        this.Root         = QueryNode("lobby");
        this.EnterButton  = QueryNode("lobby-enter");
        this.EnterLabel   = QueryNode("lobby-enter-label");
        this.StatusLine   = QueryNode("lobby-status");
        this.ProgressFill = QueryNode("lobby-progress-bar");
        this.ProgressPct  = QueryNode("lobby-progress-pct");
        this.ProgressRail = this.Root && this.Root.querySelector ? this.Root.querySelector(".lobby-progress") : null;
        this.LevelList    = QueryNode("lobby-levels");
        this.LevelCount   = QueryNode("lobby-level-count");
        this.LevelTitle   = QueryNode("lobby-title");
        this.LevelSummary = QueryNode("lobby-lede");
        this.FeatureList  = QueryNode("lobby-chips");
        this.BlueprintSvg = QueryNode("lobby-blueprint-svg");
        this.BlueprintCap = QueryNode("lobby-blueprint-caption");
        this.ReturnButton = QueryNode("lobby-return");

        const Search       = new URLSearchParams(typeof location !== "undefined" ? location.search : "");
        this.Bypass        = Search.get("lobby") === "0" || Search.get("capture") === "1";
        this.AutoEnter     = (Search.get("autostart") === "1" || Search.get("lobby") === "1") && !this.Bypass;
        this.ActiveKey     = ResolveLevelKey(Host.LevelKey);
        this.PreviewKey    = this.ActiveKey;
        this.Visible       = !!this.Root && !this.Bypass;
        this.SceneReady    = false;
        this.FadeTimer     = 0;

        this.ConstructLevelCards();
        this.PresentLevelDetails(this.ActiveKey);

        if (this.EnterButton)
        {
            this.EnterButton.addEventListener("click", () => this.ActivateSelectedLevel());
        }
        if (this.ReturnButton)
        {
            this.ReturnButton.addEventListener("click", () => this.OpenOverlay());
        }
        if (typeof window !== "undefined")
        {
            window.addEventListener("keydown", (Event) => this.InterceptKey(Event));
        }
        if (this.Root)
        {
            this.Root.classList.toggle("hidden", !this.Visible);
        }
        this.SynchroniseBodyClass();
    }

    ConstructLevelCards()
    {
        if (!this.LevelList)
        {
            return;
        }
        if (this.LevelList.replaceChildren)
        {
            this.LevelList.replaceChildren();
        }
        else
        {
            this.LevelList.innerHTML = "";
        }
        if (this.LevelCount)
        {
            this.LevelCount.textContent = String(LevelAtlas.length);
        }

        for (const Entry of LevelAtlas)
        {
            const IsLoaded = Entry.Key === this.ActiveKey;
            const CardButton = MakeNode("button", `lobby-level${IsLoaded ? " current selected" : ""}`);
            CardButton.type = "button";
            CardButton.dataset.level = Entry.Key;
            CardButton.setAttribute("aria-pressed", IsLoaded ? "true" : "false");

            const IndexTag = MakeNode("span", "lobby-level-index", Entry.Index);
            const TextColumn = MakeNode("span", "lobby-level-body");
            TextColumn.append(MakeNode("span", "lobby-level-title", Entry.Title),
                              MakeNode("span", "lobby-level-sub", Entry.Subtitle));
            const Badge = MakeNode("span", "lobby-level-state", IsLoaded ? "Loaded" : "Select");

            CardButton.append(IndexTag, TextColumn, Badge);
            CardButton.addEventListener("click", () => this.InspectOrSwitchLevel(Entry.Key));
            CardButton.addEventListener("dblclick", () => this.SwitchToLevel(Entry.Key, true));
            this.LevelList.appendChild(CardButton);
        }
    }

    PresentLevelDetails(LevelKey)
    {
        const Entry = QueryLevelEntry(LevelKey);
        this.PreviewKey = Entry.Key;

        if (this.LevelTitle)
        {
            this.LevelTitle.textContent = Entry.Title;
        }
        if (this.LevelSummary)
        {
            this.LevelSummary.textContent = Entry.Summary;
        }
        if (this.FeatureList)
        {
            if (this.FeatureList.replaceChildren)
            {
                this.FeatureList.replaceChildren();
            }
            else
            {
                this.FeatureList.innerHTML = "";
            }
            for (const FeatureLabel of Entry.Features)
            {
                this.FeatureList.appendChild(MakeNode("li", "", FeatureLabel));
            }
        }
        if (this.BlueprintSvg)
        {
            this.BlueprintSvg.innerHTML = BlueprintMarkup[Entry.Schematic] || BlueprintMarkup.proving;
        }
        if (this.BlueprintCap)
        {
            this.BlueprintCap.textContent = `${Entry.Title} · schematic plan`;
        }

        if (this.LevelList && this.LevelList.children)
        {
            for (const Row of this.LevelList.children)
            {
                const RowKey = Row.dataset ? Row.dataset.level : "";
                const Selected = RowKey === Entry.Key;
                const Loaded = RowKey === this.ActiveKey;
                Row.classList.toggle("selected", Selected);
                Row.classList.toggle("current", Loaded);
                Row.setAttribute("aria-pressed", Selected ? "true" : "false");
                const Badge = Row.children && Row.children[2];
                if (Badge)
                {
                    Badge.textContent = Loaded ? (this.SceneReady ? "Ready" : "Loading…") : (Selected ? "Load level" : "Select");
                }
            }
        }

        this.RefreshEnterAction();
    }

    RefreshEnterAction()
    {
        if (!this.EnterButton || !this.EnterLabel)
        {
            return;
        }
        const PreviewEntry = QueryLevelEntry(this.PreviewKey);
        if (this.PreviewKey !== this.ActiveKey)
        {
            this.EnterButton.disabled = false;
            this.EnterLabel.textContent = `Load & Enter ${PreviewEntry.Title}`;
            return;
        }
        this.EnterButton.disabled = !this.SceneReady;
        this.EnterLabel.textContent = this.SceneReady ? `Enter ${PreviewEntry.Title}` : `Loading ${PreviewEntry.Title}…`;
    }

    InspectOrSwitchLevel(TargetKey)
    {
        const Resolved = ResolveLevelKey(TargetKey);
        if (Resolved === this.PreviewKey && Resolved === this.ActiveKey && this.SceneReady)
        {
            this.EnterViewport();
            return;
        }
        if (Resolved === this.PreviewKey && Resolved !== this.ActiveKey)
        {
            this.SwitchToLevel(Resolved, true);
            return;
        }
        this.PresentLevelDetails(Resolved);
    }

    ActivateSelectedLevel()
    {
        if (this.PreviewKey !== this.ActiveKey)
        {
            this.SwitchToLevel(this.PreviewKey, true);
            return;
        }
        this.EnterViewport();
    }

    SwitchToLevel(TargetKey, AutoEnter = true)
    {
        const Resolved = ResolveLevelKey(TargetKey);
        if (Resolved === this.ActiveKey)
        {
            if (this.SceneReady)
            {
                this.EnterViewport();
            }
            return;
        }
        if (typeof location !== "undefined")
        {
            location.search = FormatLevelSearch(Resolved, AutoEnter);
        }
    }

    SynchroniseBodyClass()
    {
        if (typeof document !== "undefined" && document.body && document.body.classList)
        {
            document.body.classList.toggle("lobby-open", this.Visible);
        }
    }

    QueryOpen()
    {
        return !!this.Visible;
    }

    ReportLoadingProgress(Caption, Fraction)
    {
        if (!this.Root || this.SceneReady)
        {
            return;
        }
        const Clamped = Math.min(1.0, Math.max(0.0, Number(Fraction) || 0.0));
        const Percent = Math.round(Clamped * 100);
        if (this.StatusLine)
        {
            this.StatusLine.textContent = Caption;
        }
        if (this.ProgressFill)
        {
            this.ProgressFill.style.width = `${Percent}%`;
        }
        if (this.ProgressPct)
        {
            this.ProgressPct.textContent = `${Percent}%`;
        }
        if (this.ProgressRail)
        {
            this.ProgressRail.setAttribute("aria-valuenow", String(Percent));
        }
    }

    MarkSceneReady()
    {
        this.SceneReady = true;
        const ActiveEntry = QueryLevelEntry(this.ActiveKey);
        if (this.StatusLine)
        {
            this.StatusLine.textContent = `Ready · ${ActiveEntry.Title} resident on GPU`;
        }
        if (this.ProgressFill)
        {
            this.ProgressFill.style.width = "100%";
        }
        if (this.ProgressPct)
        {
            this.ProgressPct.textContent = "100%";
        }
        if (this.Root)
        {
            this.Root.classList.add("ready");
        }
        this.PresentLevelDetails(this.PreviewKey);

        if (this.AutoEnter)
        {
            this.AutoEnter = false;
            if (typeof history !== "undefined" && history.replaceState)
            {
                history.replaceState(null, "", FormatLevelSearch(this.ActiveKey, false));
            }
            this.EnterViewport();
            return;
        }
        if (this.Visible && this.EnterButton && this.EnterButton.focus)
        {
            this.EnterButton.focus();
        }
    }

    MarkFailure(Message)
    {
        if (this.StatusLine)
        {
            this.StatusLine.textContent = `Bring-up failed · ${Message}`;
        }
        if (this.Root)
        {
            this.Root.classList.add("failed");
        }
    }

    EnterViewport()
    {
        if (!this.Visible || !this.SceneReady)
        {
            return;
        }
        this.Visible = false;
        this.SynchroniseBodyClass();
        if (this.EnterButton && this.EnterButton.blur)
        {
            this.EnterButton.blur();
        }
        if (this.Host && this.Host.Keys)
        {
            this.Host.Keys.clear();
        }
        if (!this.Root)
        {
            return;
        }
        this.Root.classList.add("leaving");
        clearTimeout(this.FadeTimer);
        this.FadeTimer = setTimeout(() =>
        {
            this.Root.classList.add("hidden");
            this.Root.classList.remove("leaving");
        }, FadeOutMilliseconds);
    }

    OpenOverlay()
    {
        if (!this.Root)
        {
            return;
        }
        clearTimeout(this.FadeTimer);
        this.Visible = true;
        this.PresentLevelDetails(this.ActiveKey);
        this.SynchroniseBodyClass();
        this.Root.classList.remove("hidden", "leaving");
        if (this.Host && this.Host.Keys)
        {
            this.Host.Keys.clear();
        }
        if (this.SceneReady && this.EnterButton && this.EnterButton.focus)
        {
            this.EnterButton.focus();
        }
    }

    InterceptKey(Event)
    {
        if (!this.Root)
        {
            return;
        }
        if (Event.target instanceof HTMLInputElement || Event.target instanceof HTMLSelectElement || Event.target instanceof HTMLTextAreaElement)
        {
            return;
        }
        if (this.Visible)
        {
            if ((Event.code === "Enter" || Event.code === "NumpadEnter") && !Event.repeat)
            {
                Event.preventDefault();
                this.ActivateSelectedLevel();
            }
            else if (Event.code === "Escape" && !Event.repeat && this.SceneReady)
            {
                Event.preventDefault();
                this.EnterViewport();
            }
            else if ((Event.code === "ArrowUp" || Event.code === "ArrowDown") && !Event.repeat)
            {
                Event.preventDefault();
                const CurrentIndex = LevelAtlas.findIndex((Entry) => Entry.Key === this.PreviewKey);
                const NextIndex = (CurrentIndex + (Event.code === "ArrowDown" ? 1 : LevelAtlas.length - 1)) % LevelAtlas.length;
                this.PresentLevelDetails(LevelAtlas[NextIndex].Key);
            }
            return;
        }
        if (Event.code === "Escape" && !Event.repeat)
        {
            if (this.Host && this.Host.Shell && this.Host.Shell.CloseLevelMenu && this.Host.Shell.QueryLevelMenuOpen())
            {
                return;
            }
            Event.preventDefault();
            this.OpenOverlay();
        }
    }
}
