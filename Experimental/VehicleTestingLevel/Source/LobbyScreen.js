//============================================================================================================================================
//                                                             LOBBYSCREEN.JS
//============================================================================================================================================
// 🧩 The testing-level lobby: the single level card, the real bring-up progress, and the hand-off into the proving grounds.

// 📝 This build has one level, so the lobby is not a selector: it names the level, repeats what the course contains, and
//    holds the viewport until the driver enters. The renderer still boots behind it, and ReportProgress drives the card,
//    so the button becomes actionable exactly when the scene is live rather than on a guessed timer.
// ⚠️ The lobby owns no scene state. It only forwards visibility to the host, which blocks driving input while the card is
//    up; `?lobby=0` skips it so headless harnesses and captures reach the frame loop without a synthetic click.

import { LevelCatalog, QueryLevel, ResolveLevelKey, ConstructLevelSearch } from "./LevelCatalog.js";

const LeavingMilliseconds = 420;   // [ms] matches the #lobby opacity transition in Style/Lobby.css

function QueryElement(Id)
{
    return document.getElementById(Id);
}

function Construct(Tag, ClassName = "", Text = "")
{
    const Element = document.createElement(Tag);
    if (ClassName)
        Element.className = ClassName;
    if (Text)
        Element.textContent = Text;
    return Element;
}

export class LobbyScreen
{
    constructor(Host)
    {
        this.Host = Host;
        this.Root = QueryElement("lobby");
        this.EnterButton = QueryElement("lobby-enter");
        this.EnterLabel = QueryElement("lobby-enter-label");
        this.StatusLine = QueryElement("lobby-status");
        this.ProgressBar = QueryElement("lobby-progress-bar");
        this.ProgressRail = this.Root ? this.Root.querySelector(".lobby-progress") : null;
        this.Levels = QueryElement("lobby-levels");
        this.LevelCount = QueryElement("lobby-level-count");
        this.LevelTitle = QueryElement("lobby-title");
        this.Lede = QueryElement("lobby-lede");
        this.Chips = QueryElement("lobby-chips");
        this.LevelKey = ResolveLevelKey(Host.LevelKey);
        this.Level = QueryLevel(this.LevelKey);
        this.Visible = !!this.Root && !this.QuerySkip();
        this.Ready = false;
        this.ExitTimer = 0;

        this.ConstructLevelList();
        this.ApplyLevelCopy();
        if (this.EnterButton)
            this.EnterButton.addEventListener("click", () => this.Enter());
        window.addEventListener("keydown", (Event) => this.HandleKey(Event));

        if (this.Root)
            this.Root.classList.toggle("hidden", !this.Visible);
        this.AssignBodyFlag();
    }

    // 📝 One row per catalogue entry. The current level is marked and pressing it enters; any other row asks the browser
    //    for that level, which then boots behind this same lobby with its own progress.
    ConstructLevelList()
    {
        if (!this.Levels)
            return;
        this.Levels.innerHTML = "";
        if (this.LevelCount)
            this.LevelCount.textContent = String(LevelCatalog.length);
        for (const Entry of LevelCatalog)
        {
            const Current = Entry.Key === this.LevelKey;
            const Row = Construct("button", Current ? "lobby-level current" : "lobby-level");
            Row.type = "button";
            Row.dataset.Level = Entry.Key;
            Row.setAttribute("aria-pressed", Current ? "true" : "false");
            Row.title = Current ? `Enter ${Entry.Title}` : `Switch to ${Entry.Title}`;
            Row.append(Construct("span", "lobby-level-index", Entry.Index),
                       this.ConstructLevelBody(Entry),
                       Construct("span", "lobby-level-state", Current ? "Current" : "Switch"));
            Row.addEventListener("click", () => this.Select(Entry.Key));
            this.Levels.appendChild(Row);
        }
    }

    ConstructLevelBody(Entry)
    {
        const Body = Construct("span", "lobby-level-body");
        Body.append(Construct("span", "lobby-level-title", Entry.Title),
                    Construct("span", "lobby-level-sub", Entry.Subtitle));
        return Body;
    }

    // 📝 The hero copy is the active level's, so a switched level never inherits the previous level's description.
    ApplyLevelCopy()
    {
        const Level = this.Level;
        if (this.LevelTitle)
            this.LevelTitle.textContent = Level.Title;
        if (this.Lede)
            this.Lede.textContent = Level.Lede;
        if (this.Chips)
        {
            this.Chips.innerHTML = "";
            for (const Item of Level.Contents)
                this.Chips.appendChild(Construct("li", "", Item));
        }
        if (this.EnterLabel && !this.Ready)
            this.EnterLabel.textContent = "Preparing the ground";
    }

    Select(Key)
    {
        const Resolved = ResolveLevelKey(Key);
        if (Resolved === this.LevelKey)
        {
            this.Enter();
            return;
        }
        location.search = ConstructLevelSearch(Resolved);
    }

    // 📝 ?lobby=0 boots straight into the level; the lobby then behaves as if it had already been entered.
    QuerySkip()
    {
        return new URLSearchParams(location.search).get("lobby") === "0";
    }

    AssignBodyFlag()
    {
        if (document.body && document.body.classList)
            document.body.classList.toggle("lobby-open", this.Visible);
    }

    // 📝 Called from the host's ReportProgress: the caption is the real bring-up stage, never a fabricated percentage.
    SetProgress(Caption, Fraction)
    {
        if (!this.Root || this.Ready)
            return;
        if (this.StatusLine)
            this.StatusLine.textContent = Caption;
        if (this.ProgressBar)
            this.ProgressBar.style.width = `${Math.round(Math.min(1.0, Math.max(0.0, Fraction)) * 100)}%`;
        if (this.ProgressRail)
            this.ProgressRail.setAttribute("aria-valuenow", String(Math.round(Math.min(1.0, Math.max(0.0, Fraction)) * 100)));
    }

    SetReady()
    {
        this.Ready = true;
        if (this.StatusLine)
            this.StatusLine.textContent = `Ready · ${this.Level.Title.toLowerCase()} loaded`;
        if (this.ProgressBar)
            this.ProgressBar.style.width = "100%";
        if (this.EnterButton)
        {
            this.EnterButton.disabled = false;
            if (this.EnterLabel)
                this.EnterLabel.textContent = `Enter ${this.Level.Title}`;
            if (this.Visible && this.EnterButton.focus)
                this.EnterButton.focus();
        }
        if (this.Root)
            this.Root.classList.add("ready");
    }

    SetFailure(Message)
    {
        if (this.StatusLine)
            this.StatusLine.textContent = `Bring-up failed · ${Message}`;
        if (this.Root)
            this.Root.classList.add("failed");
    }

    // 📝 Enter is idempotent: a click and a key press in the same frame must not start two hand-offs.
    Enter()
    {
        if (!this.Visible || !this.Ready)
            return;
        this.Visible = false;
        this.AssignBodyFlag();
        if (this.EnterButton && this.EnterButton.blur)
            this.EnterButton.blur();
        if (this.Host.Keys)
            this.Host.Keys.clear();
        this.Host.OnLobbyEntered?.();
        if (!this.Root)
            return;
        this.Root.classList.add("leaving");
        this.ExitTimer = setTimeout(() =>
        {
            this.Root.classList.add("hidden");
            this.Root.classList.remove("leaving");
        }, LeavingMilliseconds);
    }

    Show(AnnounceChange = true)
    {
        if (!this.Root)
            return;
        if (this.ExitTimer)
        {
            clearTimeout(this.ExitTimer);
            this.ExitTimer = 0;
        }
        this.Visible = true;
        this.AssignBodyFlag();
        this.Root.classList.remove("hidden", "leaving");
        if (this.Host.Keys)
            this.Host.Keys.clear();
        if (this.StatusLine && this.Ready)
            this.StatusLine.textContent = "Paused at the lobby · enter when ready";
        if (this.EnterButton && this.Ready && this.EnterButton.focus)
            this.EnterButton.focus();
        if (AnnounceChange)
            this.Host.Shell?.Announce("Lobby — press Enter to return to the proving grounds");
    }

    Toggle()
    {
        if (this.Visible)
            return;
        this.Show();
    }

    HandleKey(Event)
    {
        // 📝 Typing in an inspector field must not enter the level or pop the lobby open.
        const Target = Event.target;
        if (Target && (Target.tagName === "INPUT" || Target.tagName === "SELECT" || Target.tagName === "TEXTAREA"))
            return;
        if (this.Visible)
        {
            if (Event.code === "Enter" || Event.code === "NumpadEnter" || Event.code === "Space")
            {
                Event.preventDefault();
                this.Enter();
            }
            return;
        }
        if (Event.code === "Escape" && !Event.repeat && !Event.ctrlKey && !Event.metaKey && !Event.altKey)
        {
            Event.preventDefault();
            this.Show();
        }
    }
}
