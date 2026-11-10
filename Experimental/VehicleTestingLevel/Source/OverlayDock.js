//============================================================================================================================================
//                                                              OVERLAYDOCK.JS
//============================================================================================================================================
// 🧩 The debug overlays — readout, distance-slice inset, and the outliner and inspector columns — closable one at a time, recalled from a dock.

// 📝 Four overlays: the heads-up readout (top left of the viewport), the distance-slice inset the present pass draws
//    (top right, with its HTML legend and close button framed over it), and the editor's two columns. Each column has
//    its own close button, its own visibility key and its own dock button, so closing the outliner leaves the inspector
//    standing and the viewport only takes the room that was actually freed. P still folds both columns together and V
//    hides everything for a clean view. Choices persist across reloads; the older single `Panel` entry is migrated.

const StorageKey = "IlluminationExhibit.Overlays";

export const OverlayKeys = Object.freeze({ Hud: "KeyH", Inset: "KeyJ", Panels: "KeyP", Clean: "KeyV" });

const OverlayCaptions = Object.freeze({ Hud: "Readout (H)", Inset: "Distance slice (J)", Outliner: "Outliner", Inspector: "Inspector" });
const SettingKeys = Object.freeze({ Hud: "HudVisible", Inset: "InsetEnabled", Outliner: "OutlinerVisible", Inspector: "InspectorVisible" });
const PanelNames = Object.freeze(["Outliner", "Inspector"]);

export class OverlayDock
{
    // 📝 Settings carries HudVisible, InsetEnabled (read by the present pass), OutlinerVisible and InspectorVisible;
    //    OnChange(Name) lets the host resynchronise and rebuild screen targets when a column's width enters or leaves.
    constructor(Settings, OnChange)
    {
        this.Settings = Settings;
        this.OnChange = OnChange;
        this.Clean    = false;
        this.Retained = null;
        this.Elements = {
            Hud:    document.getElementById("hud"),
            Inset:  document.getElementById("inset-frame"),
            Dock:   document.getElementById("overlay-dock"),
            Notice: document.getElementById("overlay-notice")
        };
        this.RestoreChoices();
        this.ConstructDock();
        for (const Button of document.querySelectorAll("[data-overlay]"))
            Button.addEventListener("click", (Event) => { Event.stopPropagation(); this.Toggle(Button.dataset.overlay); });
        this.Apply();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   STATE
    //--------------------------------------------------------------------------------------------------------------------

    RestoreChoices()
    {
        try
        {
            const Stored = JSON.parse(localStorage.getItem(StorageKey) || "null");
            if (!Stored || typeof Stored !== "object")
                return;
            // 📝 Builds before the split stored a single `Panel`; apply it to both columns so the remembered choice survives.
            if (typeof Stored.Panel === "boolean")
                for (const Name of PanelNames)
                    if (typeof Stored[Name] !== "boolean")
                        Stored[Name] = Stored.Panel;
            for (const Name of [...PanelNames, "Hud", "Inset"])
                if (typeof Stored[Name] === "boolean")
                    this.Settings[SettingKeys[Name]] = Stored[Name];
        }
        catch (Failure)
        {
            // 📝 Storage may be unavailable (private mode, file://); the defaults simply stand.
        }
    }

    PersistChoices()
    {
        try
        {
            localStorage.setItem(StorageKey, JSON.stringify({
                Hud: !!this.Settings.HudVisible,
                Inset: !!this.Settings.InsetEnabled,
                Outliner: !!this.Settings.OutlinerVisible,
                Inspector: !!this.Settings.InspectorVisible
            }));
        }
        catch (Failure)
        {
        }
    }

    QueryVisible(Name)
    {
        if (this.Clean)
            return false;
        if (Name === "Panels")
            return PanelNames.every((Panel) => !!this.Settings[SettingKeys[Panel]]);
        const Key = SettingKeys[Name];
        return Key ? !!this.Settings[Key] : false;
    }

    Toggle(Name)
    {
        if (Name === "Clean")
        {
            this.ToggleClean();
            return;
        }
        if (Name === "Panels")
        {
            // 📝 P folds both columns; when either one is already folded it brings both back.
            const Show = !this.QueryVisible("Panels");
            for (const Panel of PanelNames)
                this.Settings[SettingKeys[Panel]] = Show;
            this.LeaveClean();
            this.Apply();
            this.OnChange("Panels");
            return;
        }
        const Key = SettingKeys[Name];
        if (!Key)
            return;
        if (this.Clean)
        {
            // 📝 Asking for one overlay out of the clean view brings the view back first, then shows that overlay.
            this.Clean = false;
            this.Settings[Key] = true;
        }
        else
        {
            this.Settings[Key] = !this.Settings[Key];
        }
        this.Apply();
        this.OnChange(Name);
    }

    LeaveClean()
    {
        if (this.Clean)
            this.Clean = false;
    }

    ToggleClean()
    {
        this.Clean = !this.Clean;
        this.Apply();
        this.OnChange("Clean");
        if (this.Clean)
            this.Announce("Clean view — press V to bring the overlays back");
    }

    // 📝 Called when the panel's own toggles change a visibility key, so the dock and frames follow.
    Synchronise()
    {
        this.Apply();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                PRESENTATION
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 The editor's viewport bar carries the readout and inset toggles; the dock brings back whichever column is closed.
    ConstructDock()
    {
        this.DockButtons = {};
        for (const Name of PanelNames)
        {
            const Button = document.createElement("button");
            Button.textContent = `⊞ ${OverlayCaptions[Name]}`;
            Button.title = `Show the ${OverlayCaptions[Name].toLowerCase()}`;
            Button.addEventListener("click", () => this.Toggle(Name));
            this.Elements.Dock.appendChild(Button);
            this.DockButtons[Name] = Button;
        }
    }

    Apply()
    {
        const Hud = this.QueryVisible("Hud"), Inset = this.QueryVisible("Inset");
        const Visible = { Outliner: this.QueryVisible("Outliner"), Inspector: this.QueryVisible("Inspector") };
        this.Elements.Hud.classList.toggle("hidden", !Hud);
        this.Elements.Inset.classList.toggle("hidden", !Inset);
        // 📝 Each column folds on its own; the grid gives its width to the viewport without touching the other column.
        document.body.classList.toggle("outliner-hidden", !Visible.Outliner);
        document.body.classList.toggle("inspector-hidden", !Visible.Inspector);
        // 📝 The present pass draws the inset; in the clean view it must stop drawing it without forgetting the choice.
        this.InsetDrawn = Inset;
        let Closed = 0;
        for (const Name of PanelNames)
        {
            this.DockButtons[Name].classList.toggle("hidden", Visible[Name]);
            Closed += Visible[Name] ? 0 : 1;
        }
        this.Elements.Dock.classList.toggle("hidden", this.Clean || Closed === 0);
        this.PersistChoices();
    }

    Announce(Text)
    {
        const Notice = this.Elements.Notice;
        if (!Notice)
            return;
        Notice.textContent = Text;
        Notice.classList.remove("hidden");
        clearTimeout(this.NoticeTimer);
        this.NoticeTimer = setTimeout(() => Notice.classList.add("hidden"), 2600);
    }

    // 📝 Frames the HTML close button and legend over the inset DisplayPresent.wgsl draws: a square of ⌊0.34·H⌋ device
    //    pixels, 16 device pixels from the top-right corner of the render target, converted here to CSS pixels.
    PlaceInset(Canvas, ScreenExtent)
    {
        if (!ScreenExtent || !Canvas.clientWidth)
            return;
        const Ratio = ScreenExtent[0] / Canvas.clientWidth;
        const Size = Math.floor(ScreenExtent[1] * 0.34);
        const Style = this.Elements.Inset.style;
        Style.left   = `${(ScreenExtent[0] - Size - 16) / Ratio}px`;
        Style.top    = `${16 / Ratio}px`;
        Style.width  = `${Size / Ratio}px`;
        Style.height = `${Size / Ratio}px`;
    }
}
