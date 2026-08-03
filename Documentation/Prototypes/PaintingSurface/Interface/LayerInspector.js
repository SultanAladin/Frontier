/*====================================================================================================================================
                                                     LAYERINSPECTOR.JS
====================================================================================================================================*/
// 🧩 Tab-summoned layer manager: [stack | layer properties] ⇄ [identity | channels], driving the live stack

import { CLASSIFICATION_LABEL, CLASSIFICATION_TINT,
         BLEND_MODES, LayerCapacity } from "../Layers/LayerStack.js";
import { CHANNEL_ORDER, CHANNEL_LABEL, CHANNEL_SLOTS } from "../Layers/ChannelSet.js";
import { CHANNEL_MODES, LAYER_KINDS, LAYER_KIND_ORDER, KindLabel, KindTint,
         DefaultChannels } from "../Layers/LayerKinds.js";
import { MASK_COMPONENT_CATEGORY, MASK_COMPONENT_ORDER, MASK_COMPONENT_PARAMS,
         MaskFillValue } from "../Layers/LayerMask.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                        ARTWORK
//------------------------------------------------------------------------------------------------------------------------

// Darken a hex toward black by 0-1, for the shaded faces of the classification glyphs.
function Shade(Hex, Amount)
{
    const Packed = parseInt(Hex.slice(1), 16);
    const Red    = Math.round(((Packed >> 16) & 255) * (1 - Amount));
    const Green  = Math.round(((Packed >>  8) & 255) * (1 - Amount));
    const Blue   = Math.round(( Packed        & 255) * (1 - Amount));
    return "#" + ((1 << 24) + (Red << 16) + (Green << 8) + Blue).toString(16).slice(1);
}

const SvgWrap = (Body, Size) =>
    `<svg viewBox="0 0 24 24" width="${Size}" height="${Size}" fill="none" ` +
    `stroke-linecap="round" stroke-linejoin="round">${Body}</svg>`;

// 💡 Each glyph is a function of the hue, so the same art recolours per classification.
const CLASSIFICATION_ART = {
    material: (H) => `
        <path d="M12 3 L20.5 7.5 L12 12 L3.5 7.5 Z" fill="${H}"/>
        <path d="M3.5 7.5 L12 12 L12 21 L3.5 16.5 Z" fill="${Shade(H, .34)}"/>
        <path d="M20.5 7.5 L12 12 L12 21 L20.5 16.5 Z" fill="${Shade(H, .56)}"/>`,
    generator: (H) => `
        <path d="M12 2.5 L13.7 8.3 L19.5 10 L13.7 11.7 L12 17.5 L10.3 11.7 L4.5 10 L10.3 8.3 Z" fill="${H}"/>
        <circle cx="18" cy="18" r="2.1" fill="${Shade(H, .3)}"/>
        <circle cx="6.4" cy="17.4" r="1.4" fill="${Shade(H, .45)}"/>`,
    brushwork: (H) => `
        <path d="M14.6 3.6 L20.4 9.4 L11 18.8 L5.2 13 Z" fill="${H}"/>
        <path d="M5.2 13 L11 18.8 L8.4 21.4 L3.2 21.4 L2.6 16.2 Z" fill="${Shade(H, .4)}"/>
        <path d="M14.6 3.6 L20.4 9.4 L17.8 12 L12 6.2 Z" fill="${Shade(H, .6)}"/>`,
    flood: (H) => `
        <path d="M11.4 2.8 L20.6 12 L12.3 20.3 L3.1 11.1 Z" fill="${H}"/>
        <path d="M11.4 2.8 L20.6 12 L12.3 20.3 L11.4 19.4 L11.4 2.8 Z" fill="${Shade(H, .42)}"/>
        <path d="M3.1 11.1 L12.3 20.3 L12.3 14.4 L6.2 14.4 Z" fill="${Shade(H, .62)}"/>`
};

const Hue = (Classification) => CLASSIFICATION_TINT[Classification] ?? "#5b8cff";

const ClassificationSvg = (Classification, Size) =>
    SvgWrap(CLASSIFICATION_ART[Classification] ? CLASSIFICATION_ART[Classification](Hue(Classification)) : "", Size);

// 📝 Chrome stays monochrome currentColor — only classification art carries hue.
const Stroked = 'stroke="currentColor" stroke-width="1.7" fill="none"';
const GLYPH = {
    stack:       `<path ${Stroked} d="M12 3 L21 8 L12 13 L3 8 Z"/><path ${Stroked} d="M3 12.5 L12 17.5 L21 12.5"/><path ${Stroked} d="M3 16.5 L12 21.5 L21 16.5"/>`,
    search:      `<circle ${Stroked} cx="10.5" cy="10.5" r="6.5"/><path ${Stroked} d="M15.5 15.5 L21 21"/>`,
    chevron:     `<path ${Stroked} d="M8 5 L15 12 L8 19"/>`,
    chevronDown: `<path ${Stroked} d="M5 8 L12 15 L19 8"/>`,
    chevronLeft: `<path ${Stroked} d="M15 5 L8 12 L15 19"/>`,
    plus:        `<path ${Stroked} d="M12 5 V19 M5 12 H19"/>`,
    rename:      `<path ${Stroked} d="M4 20 h4 L20 8 l-4-4 L4 16 Z"/><path ${Stroked} d="M14.5 5.5 L18.5 9.5"/>`,
    eyeOpen:     `<path ${Stroked} d="M2.5 12 S6 5.5 12 5.5 S21.5 12 21.5 12 S18 18.5 12 18.5 S2.5 12 2.5 12 Z"/><circle ${Stroked} cx="12" cy="12" r="2.7"/>`,
    eyeOff:      `<path ${Stroked} d="M4 4 L20 20"/><path ${Stroked} d="M9.3 5.9 A9.8 9.8 0 0 1 12 5.5 C18 5.5 21.5 12 21.5 12 a17 17 0 0 1-2.7 3.5"/><path ${Stroked} d="M6.3 8 A16 16 0 0 0 2.5 12 S6 18.5 12 18.5 a9.6 9.6 0 0 0 3-.5"/>`,
    arrowUp:     `<path ${Stroked} d="M12 19 V5 M6 11 L12 5 L18 11"/>`,
    arrowDown:   `<path ${Stroked} d="M12 5 V19 M6 13 L12 19 L18 13"/>`,
    trash:       `<path ${Stroked} d="M4.5 7 H19.5 M9.5 7 V4.8 h5 V7 M6.5 7 l1 12.5 h9 L17.5 7"/><path ${Stroked} d="M10.3 10.5 v6 M13.7 10.5 v6"/>`,
    sliders:     `<path ${Stroked} d="M4 7 H20 M4 12 H20 M4 17 H20"/><circle ${Stroked} cx="9" cy="7" r="2"/><circle ${Stroked} cx="15" cy="12" r="2"/><circle ${Stroked} cx="8" cy="17" r="2"/>`,
    cube:        `<path ${Stroked} d="M12 3 L20.5 7.5 V16.5 L12 21 L3.5 16.5 V7.5 Z"/><path ${Stroked} d="M3.5 7.5 L12 12 L20.5 7.5 M12 12 V21"/>`,
    bucket:      `<path ${Stroked} d="M11 3 L20 12 L12 20 L3 11 Z"/><path ${Stroked} d="M18 16.5 c1.6 2.2 2.4 3.5 2.4 4.3 a2.4 2.4 0 0 1 -4.8 0 c0 -0.8 0.8 -2.1 2.4 -4.3 Z"/>`,
    // a circle half-filled — the universal "mask" read: a shape whose one half is painted through.
    mask:        `<circle ${Stroked} cx="12" cy="12" r="8.5"/><path d="M12 3.5 a8.5 8.5 0 0 1 0 17 Z" fill="currentColor"/>`
};

const Icon = (Name, Size = 15) => SvgWrap(GLYPH[Name] ?? "", Size);

//------------------------------------------------------------------------------------------------------------------------
//                                                     CHANNEL SCHEMA
//------------------------------------------------------------------------------------------------------------------------

// 🔴 SIX channels, not the inspector prototype's nine. Specular, clearcoat and ambient occlusion were
//    explicitly excluded from scope, and — more to the point — the engine has no storage for them: the
//    three RGBA8 atlases are fully spoken for. Showing a control for a channel the paint pass cannot
//    write would edit a value that never reaches a texel, which reads as "the slider does nothing".
//
// 📝 Driven off CHANNEL_ORDER/CHANNEL_SLOTS so the panel cannot drift from what the atlases store.
// 🔴 Keyed by the CHANNEL KEY, not by the display label. The emissive channel's key is "emission" while
//    its label is "Emissive", and an earlier revision keyed this table "emissive": the spread below then
//    resolved to undefined for that channel, so it fell through to the scalar branch of BuildChannelBody
//    with Min/Max/Step all undefined and offered an amount slider where a colour field belongs. Nothing
//    threw — a slider over an undefined range still renders — so the panel just quietly lost the ability
//    to author an emissive colour. The assert below is what stops the same typo returning.
const CHANNEL_EDIT = {
    baseColour: { Edit: "colour" },
    metallic:   { Edit: "scalar", Min: 0, Max: 1, Step: 0.01 },
    roughness:  { Edit: "scalar", Min: 0, Max: 1, Step: 0.01 },
    emission:   { Edit: "colour" },
    normal:     { Edit: "derived" },
    height:     { Edit: "scalar", Min: 0, Max: 1, Step: 0.01 }
};

const CHANNEL_PANELS = CHANNEL_ORDER.map((Key) => {
    const Edit = CHANNEL_EDIT[Key];
    if (!Edit) { console.error(`LayerInspector: CHANNEL_EDIT has no entry for channel "${Key}"`); }
    return {
        Key,
        Label: CHANNEL_LABEL[Key] ?? Key,
        Kind:  CHANNEL_SLOTS[Key]?.Kind ?? "scalar",
        ...Edit
    };
});

const DecimalsFor = (Step) => (Step >= 1 ? 0 : (Step >= 0.1 ? 1 : 2));

// The channels a layer paints, always as an array.
//
// 🔴 The inspector is handed LIVE `PaintLayer` objects, where the field is `Enabled` and is a **Set** —
//    `Channels` is an array that exists only on the serialized `ReadLayerStack()` snapshot. Reading
//    `Layer.Channels` off a live layer yields undefined and throws on `.includes`, which killed the whole
//    of RenderStack: the card opened with an empty list and no row ever appeared. Normalising in one place
//    keeps every call site blind to which of the two shapes it was given.
function ChannelsOf(Layer)
{
    if (Array.isArray(Layer.Channels)) { return Layer.Channels; }
    if (Layer.Enabled instanceof Set)  { return [...Layer.Enabled]; }
    return Array.isArray(Layer.Enabled) ? Layer.Enabled : [];
}

// Does this layer paint the given channel? Order-independent, so it works for either shape.
const PaintsChannel = (Layer, Key) => ChannelsOf(Layer).includes(Key);

// A layer's channel value as a CSS colour, or null when the channel is off. Used for the row thumbnail.
function SwatchOf(Layer)
{
    if (!PaintsChannel(Layer, "baseColour")) { return null; }
    const Value = Layer.Values.baseColour;
    if (!Array.isArray(Value)) { return null; }
    return ColourToHex(Value);
}

function ColourToHex(Triple)
{
    const Byte = (V) => Math.max(0, Math.min(255, Math.round((V ?? 0) * 255)))
                            .toString(16).padStart(2, "0");
    return "#" + Byte(Triple[0]) + Byte(Triple[1]) + Byte(Triple[2]);
}

function HexToColour(Hex)
{
    const Packed = parseInt(Hex.slice(1), 16);
    return [((Packed >> 16) & 255) / 255, ((Packed >> 8) & 255) / 255, (Packed & 255) / 255];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       MARKUP
//------------------------------------------------------------------------------------------------------------------------

// 📝 One template string rather than a tree of createElement calls: the shell is static and every
//    dynamic part is filled by a Render* method against an id, exactly as the prototype does it.
const InspectorMarkup = `
<div class="summon-veil" data-part="Veil"></div>
<div class="summon-menu" data-part="Menu">
  <div class="menu-view">
    <div class="menu-track" data-part="Track">

      <div class="menu-slide">
        <div class="slide-pane rail">
          <div class="pane-head">
            <span class="h-ic" data-part="StackIcon"></span>
            <span class="h-txt">
              <span class="h-lbl">Layers</span>
              <span class="fn">Suzanne</span>
            </span>
            <span class="h-n" data-part="Tally">0</span>
          </div>
          <div class="search-wrap">
            <label class="search">
              <span data-part="SearchIcon"></span>
              <input type="text" data-part="Filter" placeholder="Filter…" spellcheck="false">
            </label>
          </div>
          <div class="stack-body no-bar" data-part="StackBody"></div>
          <div class="pane-foot" data-part="StackFoot"></div>
        </div>

        <div class="slide-pane detail">
          <div class="pane-head stepper" data-part="AdvanceHead" title="Channels">
            <span class="h-ic" data-part="MetaIcon"></span>
            <span class="h-txt">
              <span class="h-lbl" data-part="MetaName">Nothing selected</span>
              <span class="fn" data-part="MetaClass">—</span>
            </span>
            <span class="h-step" data-part="AdvanceStep"></span>
          </div>
          <div class="meta-body no-bar" data-part="MetaBody"></div>
          <div class="pane-foot" data-part="MetaFoot"></div>
        </div>
      </div>

      <div class="menu-slide">
        <div class="slide-pane rail">
          <div class="pane-head stepper back" data-part="ReturnHead" title="Back to the layer stack">
            <span class="h-back" data-part="ReturnStep"></span>
            <span class="h-txt">
              <span class="h-lbl">Back</span>
              <span class="fn">Layers</span>
            </span>
          </div>
          <div class="ident-body no-bar" data-part="IdentityBody"></div>
          <div class="pane-foot" data-part="IdentityFoot"></div>
        </div>

        <div class="slide-pane detail">
          <div class="pane-head">
            <span class="h-ic" data-part="ChannelIcon"></span>
            <span class="h-txt">
              <span class="h-lbl" data-part="ChannelName">Nothing selected</span>
              <span class="fn" data-part="ChannelSub">—</span>
            </span>
          </div>
          <div class="inspect-viewport">
            <div class="inspect-track">
              <div class="inspect-pane">
                <div class="prop-body no-bar" data-part="ChannelBody"></div>
              </div>
            </div>
          </div>
          <div class="pane-foot" data-part="ChannelFoot"></div>
        </div>
      </div>

    </div>
  </div>
</div>`;

const MenuWidth  = 548;
const MenuHeight = 372;
const MenuPad    = 14;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The summoned layer manager.
//
// `Stack` is the live LayerStack; `Commands` is the command surface (the same one the harness drives, so
// the UI and the probe cannot diverge); `OnChange` is called after any mutation so the host can redraw.
// `Capture` renders a channel thumbnail and resolves to a data address, or null when the channel has no
// storage yet.
//
// 🔴 Every mutation goes through `Commands`, never through the layer objects directly. Writing
//    `Layer.Opacity = x` from here would change the value without bumping the stack revision, and the
//    compositor early-returns on an unchanged revision — the slider would move and the viewport would not.
export class LayerInspector
{
    constructor(Host, Stack, Commands, OnChange, Capture)
    {
        this.Stack    = Stack;
        this.Commands = Commands;
        this.OnChange = OnChange ?? (() => {});
        // 📝 Defaults to "no preview available" rather than throwing, so the panel is still usable when
        //    constructed without a GPU capture path (as an isolated DOM test would).
        this.Capture  = Capture ?? (async () => null);

        this.Root = document.createElement("div");
        this.Root.className = "layer-inspector";
        this.Root.innerHTML = InspectorMarkup;
        Host.appendChild(this.Root);

        this.Part = {};
        for (const Node of this.Root.querySelectorAll("[data-part]"))
        {
            this.Part[Node.dataset.part] = Node;
        }

        this.OpenState  = false;
        this.FilterTerm = "";
        this.Collapsed  = new Set();
        this.RowDragged   = false;
        this.OpenList     = null;
        this.AddLayerList = null;

        this.Part.StackIcon.innerHTML   = Icon("stack", 18);
        this.Part.SearchIcon.innerHTML  = Icon("search", 14);
        this.Part.AdvanceStep.innerHTML = Icon("chevron", 13);
        this.Part.ReturnStep.innerHTML  = Icon("chevronLeft", 14);

        this.Bind();
    }

    get Open()      { return this.OpenState; }
    get OnChannels() { return this.Part.Track.classList.contains("to-inspect"); }

    Bind()
    {
        this.Part.Veil.onpointerdown       = () => this.Hide();
        this.Part.AdvanceHead.onclick      = () => this.ShowChannels();
        this.Part.ReturnHead.onclick       = () => this.ShowStack();
        this.Part.Filter.oninput           = () => { this.FilterTerm = this.Part.Filter.value.trim().toLowerCase();
                                                     this.RenderStack(); };
        // 📝 Keystrokes inside the filter must not reach the page's shortcut handler, or typing "e" in
        //    the box toggles the eraser.
        this.Part.Filter.onkeydown         = (Event) => {
            Event.stopPropagation();
            if (Event.key === "Escape") { this.Part.Filter.blur(); }
        };

        // A dropdown is position:fixed and placed by script, so any scroll or outside press must close it.
        this.CloseListOnOutside = (Event) => {
            if (this.OpenList && !this.OpenList.contains(Event.target)) { this.CloseLists(); }
        };
        document.addEventListener("pointerdown", this.CloseListOnOutside, true);
        document.addEventListener("scroll", () => this.CloseLists(), true);
    }

    //--------------------------------------------------------------------------------------------------
    //                                        VISIBILITY
    //--------------------------------------------------------------------------------------------------

    // Open the card, clamped so it never lands off-screen.
    Show(X, Y)
    {
        const Left = Math.min(Math.max(MenuPad, X), window.innerWidth  - MenuWidth  - MenuPad);
        const Top  = Math.min(Math.max(MenuPad, Y), window.innerHeight - MenuHeight - MenuPad);
        this.Part.Menu.style.left = `${Left}px`;
        this.Part.Menu.style.top  = `${Top}px`;
        this.Part.Menu.classList.add("open");
        this.Part.Veil.classList.add("open");
        this.OpenState = true;
        this.Refresh();
    }

    // Open centred over the host element.
    ShowCentred()
    {
        const Box = this.Root.parentElement.getBoundingClientRect();
        this.Show(Box.left + Math.round((Box.width - MenuWidth) / 2), Box.top + 52);
    }

    Hide()
    {
        this.Part.Menu.classList.remove("open");
        this.Part.Veil.classList.remove("open");
        this.Part.Track.classList.remove("to-inspect");
        this.OpenState = false;
        this.CloseLists();
    }

    ShowChannels()
    {
        this.CloseLists();
        this.Part.Track.classList.add("to-inspect");
        this.RenderChannels();
        this.RenderIdentity();
    }

    ShowStack()
    {
        this.CloseLists();
        this.Part.Track.classList.remove("to-inspect");
        this.RenderProperties();
    }

    // Tab steps forward: closed → stack → channels → closed.
    Advance()
    {
        if (!this.OpenState)   { this.ShowCentred();  return; }
        if (!this.OnChannels)  { this.ShowChannels(); return; }
        this.Hide();
    }

    // Escape unwinds the same path one step at a time.
    Retreat()
    {
        if (this.OpenList)    { this.CloseLists(); return true; }
        if (!this.OpenState)  { return false; }
        if (this.OnChannels)  { this.ShowStack(); return true; }
        this.Hide();
        return true;
    }

    // Rebuild every pane. Called after a mutation and on open.
    //
    // 🔴 The preview generation is bumped FIRST, before anything is rebuilt. Every in-flight channel
    //    readback captured the previous value and drops its result on resolving, so a slow capture for a
    //    layer that is no longer focused cannot paint itself into the panel that replaced it.
    Refresh()
    {
        this.PreviewGeneration = (this.PreviewGeneration ?? 0) + 1;
        this.RenderStack();
        this.RenderProperties();
        this.RenderIdentity();
        this.RenderChannels();
    }

    // Push a command at the stack, then rebuild and tell the host to redraw.
    Apply(Command, Payload)
    {
        const Outcome = this.Commands(Command, Payload);
        this.Refresh();
        this.OnChange();
        return Outcome;
    }

    //--------------------------------------------------------------------------------------------------
    //                                        STACK PANE
    //--------------------------------------------------------------------------------------------------

    RenderStack()
    {
        const Body = this.Part.StackBody;
        Body.innerHTML = "";

        const Layers = this.Stack.Layers.filter(
            (L) => !this.FilterTerm || L.Name.toLowerCase().includes(this.FilterTerm));

        for (const Layer of Layers)
        {
            Body.appendChild(this.BuildRow(Layer));

            // 🔴 The active row's settings drop open INLINE, right beneath it in the rail. Only the focused
            //    layer expands, and it is skipped while a filter is active — a filtered stack is a lookup,
            //    not an editing surface, and an accordion inside it fights the row the user is scanning for.
            if (Layer.Token === this.Stack.FocusToken && !this.FilterTerm)
            {
                Body.appendChild(this.BuildExpand(Layer));
            }
        }

        if (Body.children.length === 0)
        {
            Body.innerHTML = '<div class="empty-state">No layers match.<br>Clear the filter to see the stack.</div>';
        }

        this.Part.Tally.textContent = this.Stack.Count;

        this.RenderStackFoot();
    }

    // The stack footer: the layer tally, and the "+ Add Layer" affordance that unfolds one option per
    // layer kind (Paint / Fill / Material / Generator, straight from LAYER_KIND_ORDER).
    //
    // 🔴 A new layer is created through the "add" verb, never by touching the stack directly — the verb
    //    seeds a fill/material's content and re-flattens, which a bare Stack.Add would skip.
    RenderStackFoot()
    {
        const Foot = this.Part.StackFoot;
        Foot.innerHTML = "";

        const Hidden = this.Stack.Layers.filter((L) => !L.Shown).length;
        const Tally  = document.createElement("span");
        Tally.className = "sf-tally";
        Tally.innerHTML =
            `<span class="pf-strong">${this.Stack.Count}</span> / ${LayerCapacity}` +
            (Hidden ? `<span class="pf-dot">·</span><span>${Hidden} hidden</span>` : "");
        Foot.appendChild(Tally);

        const Spacer = document.createElement("span");
        Spacer.className = "pf-spacer";
        Foot.appendChild(Spacer);

        const AtCap = this.Stack.Count >= LayerCapacity;
        const Add   = document.createElement("div");
        Add.className = "sf-add" + (AtCap ? " disabled" : "");
        Add.innerHTML = `${Icon("plus", 12)}<span>Add Layer</span>`;
        Add.title = AtCap ? `Layer cap of ${LayerCapacity} reached` : "Add a layer";

        if (!AtCap)
        {
            Add.onclick = (Event) => {
                Event.stopPropagation();
                this.OpenAddLayerList(Add);
            };
        }
        Foot.appendChild(Add);
    }

    // The add-layer kind list, placed above the footer button. Reuses the fixed-position dropdown
    // machinery so it escapes the scrolling rail and closes on any outside press.
    OpenAddLayerList(Anchor)
    {
        const WasOpen = this.OpenList === this.AddLayerList;
        this.CloseLists();
        if (WasOpen) { return; }

        const List = document.createElement("div");
        List.className = "dd-list sf-add-list open";

        for (const Kind of LAYER_KIND_ORDER)
        {
            const Item = document.createElement("div");
            Item.className = "dd-item";
            Item.innerHTML =
                `<span class="sf-swatch" style="background:${KindTint(Kind)}"></span>` +
                `<span>${KindLabel(Kind)}</span>`;
            Item.onclick = (Event) => {
                Event.stopPropagation();
                this.CloseLists();
                this.AddLayer(Kind);
            };
            List.appendChild(Item);
        }

        document.body.appendChild(List);
        this.AddLayerList = List;
        this.OpenList     = List;

        const Box  = Anchor.getBoundingClientRect();
        const Tall = LAYER_KIND_ORDER.length * 29 + 8;
        List.style.left  = `${Box.left}px`;
        List.style.width = `${Math.max(Box.width, 132)}px`;
        // The footer sits at the bottom of the card, so the list always opens UPWARD from the button.
        List.style.top   = `${Box.top - Tall - 4}px`;
    }

    // Create a layer of the given kind through the add verb.
    //
    // 🔴 A fill or material is FLOODED at full coverage across every channel — a base material wants the
    //    whole atlas including the UV gutters — while a paint or generator layer starts empty. The Flood
    //    payload and the default channel set both follow from the kind, so the kind is the single input.
    AddLayer(Kind)
    {
        const Flood    = Boolean(LAYER_KINDS[Kind]?.Flooded);
        const Channels = DefaultChannels(Kind, null, null);
        this.Apply("add", {
            Name:     `NEW_${KindLabel(Kind)}`,
            Kind:     Kind,
            Channels: Channels,
            Flood:    Flood
        });
    }

    BuildRow(Layer)
    {
        const Row = document.createElement("div");
        Row.className = "stack-row" +
            (Layer.Token === this.Stack.FocusToken ? " active" : "") +
            (Layer.Shown ? "" : " layer-muted");
        Row.dataset.token = Layer.Token;

        const Tint   = Hue(Layer.Classification);
        const Swatch = SwatchOf(Layer);
        const Thumb  = Swatch
            ? `<span class="sr-thumb-fill" style="background:${Swatch}"></span>`
            : ClassificationSvg(Layer.Classification, 15);

        Row.innerHTML =
            `<span class="sr-tag" style="background:${Tint}"></span>` +
            `<span class="sr-thumb">${Thumb}</span>` +
            `<span class="sr-text">` +
              `<span class="sr-name"></span>` +
              `<span class="sr-meta">${CLASSIFICATION_LABEL[Layer.Classification]} · ${Layer.Blend} · ` +
                `${ChannelsOf(Layer).length} ch</span>` +
            `</span>` +
            `<span class="sr-opacity" title="Drag to adjust opacity">${Layer.Opacity}%</span>` +
            `<span class="sr-visibility" title="${Layer.Shown ? "Hide" : "Show"}">` +
              `${Icon(Layer.Shown ? "eyeOpen" : "eyeOff", 14)}</span>`;

        // 🔴 The name is assigned as TEXT, not interpolated into the markup above. A layer name is
        //    user-typed, and a name containing "<" would otherwise be parsed as markup — at best the
        //    name vanishes, at worst the row's own handlers are replaced by injected ones.
        Row.querySelector(".sr-name").textContent = Layer.Name;

        Row.onclick = (Event) => {
            if (this.RowDragged) { this.RowDragged = false; return; }
            if (Event.target.closest(".sr-visibility") || Event.target.closest(".sr-opacity")) { return; }
            if (Layer.Token !== this.Stack.FocusToken) { this.Apply("focus", { Token: Layer.Token }); }
        };
        Row.ondblclick = (Event) => {
            if (Event.target.closest(".sr-visibility") || Event.target.closest(".sr-opacity")) { return; }
            this.BeginRename(Layer, Row.querySelector(".sr-name"));
        };
        // 🔴 Right-click inside the card focuses a row; it never opens a second menu, and it must
        //    swallow the event so the page's own right-click summon does not fire underneath.
        Row.oncontextmenu = (Event) => {
            Event.preventDefault();
            Event.stopPropagation();
            if (Layer.Token !== this.Stack.FocusToken) { this.Apply("focus", { Token: Layer.Token }); }
        };
        Row.querySelector(".sr-visibility").onclick = (Event) => {
            Event.stopPropagation();
            this.Apply("show", { Token: Layer.Token, Shown: !Layer.Shown });
        };
        this.BindOpacityDrag(Row.querySelector(".sr-opacity"), Layer);
        if (!this.FilterTerm) { this.BindRowDrag(Row, Layer); }

        return Row;
    }

    //--------------------------------------------------------------------------------------------------
    //                                     INLINE EXPAND
    //--------------------------------------------------------------------------------------------------

    // The active layer's settings, dropped open in the rail directly under its row: the same paint-menu
    // controls Studio carries — Visible, Blend, Opacity, base Colour — plus the mask section.
    //
    // 🔴 Every control here drives the SAME command surface the detail pane uses, so the two never diverge:
    //    a change made in the inline expand shows in the properties pane and vice versa on the next Refresh.
    BuildExpand(Layer)
    {
        const Host = document.createElement("div");
        Host.className = "stack-expand";
        const Clip = document.createElement("div"); Clip.className = "se-clip";
        const Body = document.createElement("div"); Body.className = "se-body";

        const Line = (Label, Field) => {
            const Row = document.createElement("div");
            Row.className = "se-row";
            const Tag = document.createElement("span"); Tag.className = "se-k"; Tag.textContent = Label;
            const Val = document.createElement("span"); Val.className = "se-v"; Val.appendChild(Field);
            Row.appendChild(Tag); Row.appendChild(Val);
            return Row;
        };

        // ---- Visible -----------------------------------------------------------------------------------
        Body.appendChild(Line("Visible", BuildSwitch(Layer.Shown, (On) =>
            this.Apply("show", { Token: Layer.Token, Shown: On }))));

        // ---- Blend -------------------------------------------------------------------------------------
        Body.appendChild(Line("Blend", this.BuildDropdown(BLEND_MODES, Layer.Blend, (Pick) =>
            this.Apply("blend", { Token: Layer.Token, Blend: Pick }))));

        // ---- Opacity -----------------------------------------------------------------------------------
        Body.appendChild(Line("Opacity", BuildSlider({
            Min: 0, Max: 100, Step: 1, Value: Layer.Opacity, Unit: "%",
            OnInput: (Value, Live) => {
                this.Commands("opacity", { Token: Layer.Token, Opacity: Value });
                const Pill = this.Part.StackBody
                    .querySelector(`.stack-row[data-token="${Layer.Token}"] .sr-opacity`);
                if (Pill) { Pill.textContent = `${Value}%`; }
                this.OnChange();
                if (!Live) { this.Refresh(); }
            }
        })));

        // ---- Colour ------------------------------------------------------------------------------------
        // 🔴 Only offered where the layer paints baseColour, and it edits the AUTHORED value — the ink a
        //    dab deposits on a paint layer, the flooded fill on a fill/material. A layer with no baseColour
        //    channel (a height-only generator, say) has no colour to author, so the row is simply omitted
        //    rather than shown editing a value that reaches no atlas.
        if (PaintsChannel(Layer, "baseColour"))
        {
            const Current = Array.isArray(Layer.Values.baseColour)
                ? ColourToHex(Layer.Values.baseColour) : "#808080";
            Body.appendChild(Line("Colour", BuildColourField(Current, (Hex, Live) => {
                this.Commands("value", { Token: Layer.Token, Channel: "baseColour", Value: HexToColour(Hex) });
                this.OnChange();
                if (!Live) { this.Refresh(); }
            })));
        }

        // ---- Mask --------------------------------------------------------------------------------------
        this.BuildMaskSection(Body, Layer);

        Clip.appendChild(Body);
        Host.appendChild(Clip);
        return Host;
    }

    //--------------------------------------------------------------------------------------------------
    //                                       MASK SECTION
    //--------------------------------------------------------------------------------------------------

    // The layer's mask editor, ported from Studio: an enable affordance while off, and while on a preview
    // reading fill+invert, the White/Black/Invert toolbar, an opacity slider, the component stack and an
    // add-component picker. Every control drives a mask verb on the shared command surface.
    BuildMaskSection(Body, Layer)
    {
        const Mask = Layer.Mask;

        const Sect = document.createElement("div");
        Sect.className = "se-sect";
        Sect.textContent = "Mask";
        Body.appendChild(Sect);

        // Off: a single affordance that enables the mask (which seeds its atlas).
        if (!Mask || !Mask.Enabled)
        {
            const Empty = document.createElement("div");
            Empty.className = "msk-empty";
            Empty.innerHTML = `${Icon("mask", 13)}<span>Add mask</span>`;
            Empty.onclick = () => this.Apply("mask", { Token: Layer.Token, Enabled: true });
            Body.appendChild(Empty);
            return;
        }

        // ---- preview + disable -------------------------------------------------------------------------
        const Base   = MaskFillValue(Mask);
        const Light  = Mask.Invert ? 1 - Base : Base;
        const Shade  = Math.round(Light * 255);
        const HeadRow = document.createElement("div");
        HeadRow.className = "msk-row";
        HeadRow.innerHTML =
            `<span class="msk-prev"><span class="msk-grad" ` +
              `style="background:rgb(${Shade},${Shade},${Shade})"></span></span>` +
            `<span class="msk-info">` +
              `<span class="msk-nm"></span>` +
              `<span class="msk-meta">${Mask.Fill === "black" ? "Black" : "White"} fill` +
                `${Mask.Invert ? " · inverted" : ""} · ${Mask.Components.length} comp</span>` +
            `</span>` +
            `<span class="mc-x" title="Remove mask" style="margin-left:auto">×</span>`;
        HeadRow.querySelector(".msk-nm").textContent = "Layer mask";
        HeadRow.querySelector(".mc-x").onclick = () =>
            this.Apply("mask", { Token: Layer.Token, Enabled: false });
        Body.appendChild(HeadRow);

        // ---- White / Black / Invert toolbar ------------------------------------------------------------
        const Bar = document.createElement("div");
        Bar.className = "msk-toolbar";

        const Tool = (Label, On, Run) => {
            const El = document.createElement("div");
            El.className = "msk-tool" + (On ? " on" : "");
            El.textContent = Label;
            El.onclick = Run;
            return El;
        };
        Bar.appendChild(Tool("White", Mask.Fill !== "black",
            () => this.Apply("maskFill", { Token: Layer.Token, Fill: "white" })));
        Bar.appendChild(Tool("Black", Mask.Fill === "black",
            () => this.Apply("maskFill", { Token: Layer.Token, Fill: "black" })));
        Bar.appendChild(Tool("Invert", Mask.Invert === true,
            () => this.Apply("maskInvert", { Token: Layer.Token, Invert: !Mask.Invert })));
        Body.appendChild(Bar);

        // ---- mask opacity ------------------------------------------------------------------------------
        const OpacityRow = document.createElement("div");
        OpacityRow.className = "se-row";
        OpacityRow.innerHTML = `<span class="se-k">Strength</span>`;
        const OpacityVal = document.createElement("span"); OpacityVal.className = "se-v";
        OpacityVal.appendChild(BuildSlider({
            Min: 0, Max: 100, Step: 1, Value: Mask.Opacity ?? 100, Unit: "%",
            OnInput: (Value, Live) => {
                this.Commands("maskOpacity", { Token: Layer.Token, Opacity: Value });
                this.OnChange();
                if (!Live) { this.Refresh(); }
            }
        }));
        OpacityRow.appendChild(OpacityVal);
        Body.appendChild(OpacityRow);

        // ---- component stack ---------------------------------------------------------------------------
        this.BuildMaskComponents(Body, Layer);
    }

    // The mask's ordered component stack, plus the add-component picker.
    BuildMaskComponents(Body, Layer)
    {
        const Mask = Layer.Mask;

        const Comps = document.createElement("div");
        Comps.className = "msk-comps";

        for (const Component of Mask.Components)
        {
            Comps.appendChild(this.BuildMaskComponent(Layer, Component));
        }
        Body.appendChild(Comps);

        // The add-component picker: one option per category (paint / fill / generator / levels).
        Body.appendChild(this.BuildDropdown(
            MASK_COMPONENT_ORDER.map((C) => MASK_COMPONENT_CATEGORY[C].Label),
            "Add component",
            (Pick) => {
                const Category = MASK_COMPONENT_ORDER.find(
                    (C) => MASK_COMPONENT_CATEGORY[C].Label === Pick);
                if (Category) { this.Apply("maskAddComponent", { Token: Layer.Token, Category }); }
            }));
    }

    // One entry in the mask component stack: its glyph, name + category, and — for a paint component — a
    // focus toggle that routes brush strokes into it; every category carries an opacity slider, a params
    // block for fill/generator/levels, and a remove ×.
    BuildMaskComponent(Layer, Component)
    {
        const Category = MASK_COMPONENT_CATEGORY[Component.Category] ?? MASK_COMPONENT_CATEGORY.fill;
        const Paintable = Category.Paintable === true;
        const Focused   = Layer.Mask.FocusToken === Component.Token;

        const Host = document.createElement("div");
        Host.className = "msk-comp";

        // ---- header row: glyph, name, focus toggle (paint only), remove --------------------------------
        const Head = document.createElement("div");
        Head.className = "msk-row";
        Head.style.margin = "0";
        Head.innerHTML =
            `<span class="mc-ico">${Icon("mask", 12)}</span>` +
            `<span class="mc-tx"><span class="mc-nm"></span>` +
              `<span class="mc-md">${Category.Label}${Focused ? " · painting" : ""}</span></span>`;
        Head.querySelector(".mc-nm").textContent = Component.Name;

        // 🔴 A PAINT component gets a focus toggle: focusing it is what makes a brush stroke land in the
        //    mask rather than the layer's channels (ResolveMaskPaintTarget keys off exactly this). The
        //    other categories have no atlas to paint, so they carry no toggle.
        if (Paintable)
        {
            const Aim = document.createElement("span");
            Aim.className = "msk-tool" + (Focused ? " on" : "");
            Aim.style.cssText = "flex:0 0 auto; height:22px; padding:0 8px";
            Aim.textContent = Focused ? "Painting" : "Paint";
            Aim.onclick = () => this.Apply("maskFocusComponent",
                { Token: Layer.Token, Component: Focused ? null : Component.Token });
            Head.appendChild(Aim);
        }

        const Kill = document.createElement("span");
        Kill.className = "mc-x";
        Kill.style.marginLeft = "auto";
        Kill.textContent = "×";
        Kill.onclick = () => this.Apply("maskRemoveComponent",
            { Token: Layer.Token, Component: Component.Token });
        Head.appendChild(Kill);

        Host.appendChild(Head);

        // ---- component opacity -------------------------------------------------------------------------
        const OpacityRow = document.createElement("div");
        OpacityRow.className = "se-row";
        OpacityRow.innerHTML = `<span class="se-k">Opacity</span>`;
        const OpacityVal = document.createElement("span"); OpacityVal.className = "se-v";
        OpacityVal.appendChild(BuildSlider({
            Min: 0, Max: 100, Step: 1, Value: Component.Opacity ?? 100, Unit: "%",
            OnInput: (Value, Live) => {
                this.Commands("maskComponentOpacity",
                    { Token: Layer.Token, Component: Component.Token, Opacity: Value });
                this.OnChange();
                if (!Live) { this.Refresh(); }
            }
        }));
        OpacityRow.appendChild(OpacityVal);
        Host.appendChild(OpacityRow);

        // ---- category params (fill / generator / levels) -----------------------------------------------
        for (const Spec of MASK_COMPONENT_PARAMS[Component.Category] ?? [])
        {
            const Row = document.createElement("div");
            Row.className = "se-row";
            const Tag = document.createElement("span"); Tag.className = "se-k"; Tag.textContent = Spec.Label;
            const Val = document.createElement("span"); Val.className = "se-v";
            Val.appendChild(BuildSlider({
                Min: Spec.Min, Max: Spec.Max, Step: Spec.Step,
                Value: Number(Component.Params?.[Spec.Key] ?? Category.Defaults?.[Spec.Key] ?? Spec.Min),
                OnInput: (Value, Live) => {
                    this.Commands("maskComponentParam",
                        { Token: Layer.Token, Component: Component.Token, Key: Spec.Key, Value });
                    this.OnChange();
                    if (!Live) { this.Refresh(); }
                }
            }));
            Row.appendChild(Tag); Row.appendChild(Val);
            Host.appendChild(Row);
        }

        return Host;
    }

    // The opacity pill scrubs horizontally.
    //
    // 📝 The live drag writes straight to the pill's own text and only commits through the command
    //    surface on release. Committing on every pointer move would rebuild the whole stack pane under
    //    the cursor mid-drag, which detaches the element the pointer is captured on.
    BindOpacityDrag(Pill, Layer)
    {
        Pill.onpointerdown = (Event) => {
            Event.stopPropagation();
            Event.preventDefault();
            const StartX = Event.clientX;
            const Start  = Layer.Opacity;
            let   Value  = Start;
            Pill.setPointerCapture(Event.pointerId);

            const Move = (Motion) => {
                Value = Math.max(0, Math.min(100, Start + Math.round((Motion.clientX - StartX) / 2)));
                Pill.textContent = `${Value}%`;
                this.Commands("opacity", { Token: Layer.Token, Opacity: Value });
                this.OnChange();
            };
            const Up = () => {
                Pill.removeEventListener("pointermove", Move);
                Pill.removeEventListener("pointerup", Up);
                if (Value !== Start) { this.Refresh(); }
            };
            Pill.addEventListener("pointermove", Move);
            Pill.addEventListener("pointerup", Up);
        };
    }

    // Press and drag vertically to restack.
    //
    // 🔴 The drop is applied as repeated single-step Reorder calls, because that is the only reordering
    //    primitive the stack exposes. Splicing this.Stack.Layers directly would move the row without
    //    bumping the revision, so the composite would keep the old order while the list showed the new.
    BindRowDrag(Row, Layer)
    {
        Row.addEventListener("pointerdown", (Event) => {
            if (Event.button !== 0) { return; }
            if (Event.target.closest(".sr-visibility") || Event.target.closest(".sr-opacity")) { return; }

            const StartY = Event.clientY;
            const StartX = Event.clientX;
            let Dragging = false;
            let Marker   = null;

            const Move = (Motion) => {
                if (!Dragging)
                {
                    if (Math.abs(Motion.clientY - StartY) < 5 && Math.abs(Motion.clientX - StartX) < 5) { return; }
                    Dragging = true;
                    this.RowDragged = true;
                    Row.setPointerCapture(Motion.pointerId);
                    Row.classList.add("dragging");
                    Marker = document.createElement("div");
                    Marker.className = "drop-line";
                }
                const Others = [...this.Part.StackBody.querySelectorAll(".stack-row")].filter((R) => R !== Row);
                let Placed = false;
                for (const Other of Others)
                {
                    const Box = Other.getBoundingClientRect();
                    if (Motion.clientY < Box.top + Box.height / 2)
                    {
                        this.Part.StackBody.insertBefore(Marker, Other);
                        Placed = true;
                        break;
                    }
                }
                if (!Placed) { this.Part.StackBody.appendChild(Marker); }
            };

            const Up = () => {
                Row.removeEventListener("pointermove", Move);
                Row.removeEventListener("pointerup", Up);
                if (!Dragging) { return; }
                Row.classList.remove("dragging");

                // Where the marker sits, in row terms, is the destination index.
                let Destination = 0;
                for (const Node of this.Part.StackBody.children)
                {
                    if (Node === Marker) { break; }
                    if (Node.classList.contains("stack-row") && Node !== Row) { Destination += 1; }
                }
                Marker.remove();

                const From = this.Stack.IndexOf(Layer.Token);
                // Direction −1 raises (toward index 0), matching the stack's own convention.
                const Step = Destination < From ? -1 : 1;
                for (let At = From; At !== Destination; At += Step)
                {
                    if (!this.Stack.Reorder(Layer.Token, Step)) { break; }
                }
                this.Stack.Touch();
                this.Refresh();
                this.OnChange();
            };

            Row.addEventListener("pointermove", Move);
            Row.addEventListener("pointerup", Up);
        });
    }

    // Rename in place on the row label.
    BeginRename(Layer, Label)
    {
        if (!Label) { return; }
        const Previous = Layer.Name;
        Label.classList.add("editing");
        Label.contentEditable = "true";
        Label.focus();

        const Range = document.createRange();
        Range.selectNodeContents(Label);
        const Selection = window.getSelection();
        Selection.removeAllRanges();
        Selection.addRange(Range);

        const Commit = () => {
            Label.contentEditable = "false";
            Label.classList.remove("editing");
            const Next = Label.textContent.trim();
            if (Next && Next !== Previous) { this.Apply("rename", { Token: Layer.Token, Name: Next }); }
            else                           { Label.textContent = Previous; }
        };
        Label.onblur = Commit;
        // 📝 Keystrokes are swallowed so Enter does not reach the page and "e" does not toggle erase.
        Label.onkeydown = (Event) => {
            Event.stopPropagation();
            if (Event.key === "Enter")  { Event.preventDefault(); Label.blur(); }
            if (Event.key === "Escape") { Label.textContent = Previous; Label.blur(); }
        };
    }

    //--------------------------------------------------------------------------------------------------
    //                                     PROPERTIES PANE
    //--------------------------------------------------------------------------------------------------

    RenderProperties()
    {
        const Layer = this.Stack.Focus;
        const Body  = this.Part.MetaBody;
        Body.innerHTML = "";

        if (!Layer)
        {
            this.Part.MetaName.textContent  = "Nothing selected";
            this.Part.MetaClass.textContent = "—";
            this.Part.MetaIcon.innerHTML    = Icon("stack", 17);
            Body.innerHTML = '<div class="empty-state">No layer focused.<br>Pick one in the stack.</div>';
            this.Part.MetaFoot.innerHTML = "<span>—</span>";
            return;
        }

        const Tint = Hue(Layer.Classification);
        this.Part.MetaName.textContent  = Layer.Name;
        this.Part.MetaClass.textContent = `${CLASSIFICATION_LABEL[Layer.Classification]} layer`;
        this.Part.MetaIcon.innerHTML    = ClassificationSvg(Layer.Classification, 17);

        const Swatch = SwatchOf(Layer);
        const Hero   = document.createElement("div");
        Hero.className = "meta-hero";
        Hero.innerHTML =
            `<span class="mh-ic">${Swatch
                ? `<span class="mh-fill" style="background:${Swatch}"></span>`
                : ClassificationSvg(Layer.Classification, 22)}</span>` +
            `<span class="mh-txt">` +
              `<span class="mh-name"></span>` +
              `<span class="mh-class" style="color:${Tint}">` +
                `${CLASSIFICATION_LABEL[Layer.Classification]}</span>` +
            `</span>`;
        Hero.querySelector(".mh-name").textContent = Layer.Name;
        Body.appendChild(Hero);

        Body.appendChild(SectionLabel("Properties", "sliders"));

        Body.appendChild(PropertyRow("Visible", BuildSwitch(Layer.Shown, (On) =>
            this.Apply("show", { Token: Layer.Token, Shown: On }))));

        Body.appendChild(PropertyRow("Blend", this.BuildDropdown(BLEND_MODES, Layer.Blend, (Pick) =>
            this.Apply("blend", { Token: Layer.Token, Blend: Pick }))));

        Body.appendChild(PropertyRow("Opacity", BuildSlider({
            Min: 0, Max: 100, Step: 1, Value: Layer.Opacity, Unit: "%",
            OnInput: (Value, Live) => {
                this.Commands("opacity", { Token: Layer.Token, Opacity: Value });
                const Pill = this.Part.StackBody
                    .querySelector(`.stack-row[data-token="${Layer.Token}"] .sr-opacity`);
                if (Pill) { Pill.textContent = `${Value}%`; }
                this.OnChange();
                if (!Live) { this.Refresh(); }
            }
        })));

        const ChannelRow = document.createElement("div");
        ChannelRow.className = "meta-row";
        ChannelRow.innerHTML = `<span class="mr-k">Channels</span>` +
            `<span class="mr-v">${ChannelsOf(Layer).length} / ${CHANNEL_PANELS.length} active</span>`;
        Body.appendChild(ChannelRow);

        Body.appendChild(SectionLabel("Actions"));
        Body.appendChild(this.BuildActions(Layer));

        const Cta = document.createElement("div");
        Cta.className = "meta-cta";
        Cta.innerHTML = `<span>Channels</span>${Icon("chevron", 13)}<span class="cta-kbd">Tab</span>`;
        Cta.onclick = () => this.ShowChannels();
        Body.appendChild(Cta);

        this.Part.MetaFoot.innerHTML =
            `<span class="pf-hue" style="background:${Tint}"></span>` +
            `<span>${CLASSIFICATION_LABEL[Layer.Classification]}</span>` +
            `<span class="pf-dot">·</span><span class="pf-strong">${Layer.Opacity}%</span>` +
            `<span class="pf-spacer"></span><span>${Layer.Blend}</span>`;
    }

    // 🔴 Delete is the ONE per-layer action left in the detail pane. Raise / Lower moved to the stack's
    //    own drag-reorder, and adding a layer moved to the "+ Add Layer" footer picker — leaving the
    //    right-hand action list to the single verb that has no other home.
    BuildActions(Layer)
    {
        const Host = document.createElement("div");
        Host.className = "meta-actions";

        const Item    = document.createElement("div");
        // 🔴 The stack refuses to remove its last layer, so the row is disabled rather than offered and
        //    then silently ignored.
        const Enabled = this.Stack.Count > 1;
        Item.className = "act-item danger" + (Enabled ? "" : " disabled");
        Item.innerHTML = `<span class="ic">${Icon("trash", 15)}</span><span>Delete</span>`;
        if (Enabled) { Item.onclick = () => this.Apply("remove", { Token: Layer.Token }); }
        Host.appendChild(Item);

        return Host;
    }

    //--------------------------------------------------------------------------------------------------
    //                                      IDENTITY PANE
    //--------------------------------------------------------------------------------------------------

    RenderIdentity()
    {
        const Layer = this.Stack.Focus;
        const Body  = this.Part.IdentityBody;
        Body.innerHTML = "";

        if (!Layer)
        {
            Body.innerHTML = '<div class="empty-state">No layer focused.</div>';
            this.Part.IdentityFoot.innerHTML = "<span>—</span>";
            return;
        }

        const Tint   = Hue(Layer.Classification);
        const Swatch = SwatchOf(Layer);

        const Chip = document.createElement("div");
        Chip.className = "ident-chip";
        Chip.innerHTML = Swatch
            ? `<span class="ic-fill" style="background:${Swatch}"></span>`
            : ClassificationSvg(Layer.Classification, 34);
        Body.appendChild(Chip);

        const Name = document.createElement("div");
        Name.className = "ident-name";
        Name.textContent = Layer.Name;
        Body.appendChild(Name);

        const Class = document.createElement("div");
        Class.className = "ident-class";
        Class.style.color = Tint;
        Class.textContent = CLASSIFICATION_LABEL[Layer.Classification];
        Body.appendChild(Class);

        const Facts = [
            ["Blend",    Layer.Blend],
            ["Opacity",  `${Layer.Opacity}%`],
            ["Visible",  Layer.Shown ? "Yes" : "No"],
            ["Channels", `${ChannelsOf(Layer).length} of ${CHANNEL_PANELS.length}`],
            ["Depth",    `${this.Stack.IndexOf(Layer.Token) + 1} of ${this.Stack.Count}`]
        ];
        for (const [Key, Value] of Facts)
        {
            const Row = document.createElement("div");
            Row.className = "ident-stat";
            Row.innerHTML = `<span>${Key}</span><span class="is-v"></span>`;
            Row.querySelector(".is-v").textContent = Value;
            Body.appendChild(Row);
        }

        this.Part.IdentityFoot.innerHTML =
            `<span class="pf-hue" style="background:${Tint}"></span>` +
            `<span>${CLASSIFICATION_LABEL[Layer.Classification]}</span>`;
    }

    //--------------------------------------------------------------------------------------------------
    //                                      CHANNELS PANE
    //--------------------------------------------------------------------------------------------------

    RenderChannels()
    {
        const Layer = this.Stack.Focus;
        const Body  = this.Part.ChannelBody;
        Body.innerHTML = "";

        if (!Layer)
        {
            this.Part.ChannelName.textContent = "Nothing selected";
            this.Part.ChannelSub.textContent  = "—";
            this.Part.ChannelIcon.innerHTML   = Icon("stack", 17);
            Body.innerHTML = '<div class="empty-state">No layer focused.</div>';
            this.Part.ChannelFoot.innerHTML = "<span>—</span>";
            return;
        }

        this.Part.ChannelName.textContent = Layer.Name;
        this.Part.ChannelSub.textContent  =
            `${CLASSIFICATION_LABEL[Layer.Classification]} · ${Layer.Blend}`;
        this.Part.ChannelIcon.innerHTML   = ClassificationSvg(Layer.Classification, 17);

        Body.appendChild(this.BuildChannelChips(Layer));
        for (const Panel of CHANNEL_PANELS)
        {
            if (PaintsChannel(Layer, Panel.Key)) { Body.appendChild(this.BuildChannelPanel(Layer, Panel)); }
        }

        this.Part.ChannelFoot.innerHTML =
            `<span class="pf-strong">${ChannelsOf(Layer).length}</span> channels` +
            `<span class="pf-spacer"></span><span>3 atlases</span>`;
    }

    BuildChannelChips(Layer)
    {
        const Card = document.createElement("div");
        Card.className = "card";

        const Head = document.createElement("div");
        Head.className = "card-head";
        Head.innerHTML = `<span class="ch-tw">${Icon("chevronDown", 10)}</span><span>Channels</span>` +
            `<span class="ch-n">${ChannelsOf(Layer).length} / ${CHANNEL_PANELS.length}</span>`;

        const Shell = document.createElement("div"); Shell.className = "card-shell";
        const Clip  = document.createElement("div"); Clip.className  = "card-body-inner";
        const Body  = document.createElement("div"); Body.className  = "card-body";

        const Chips = document.createElement("div");
        Chips.className = "chan-chips";

        for (const Panel of CHANNEL_PANELS)
        {
            if (!PaintsChannel(Layer, Panel.Key)) { continue; }
            const Chip = document.createElement("div");
            Chip.className = "chan-chip";
            Chip.innerHTML = `<span>${Panel.Label}</span><span class="cc-x" title="Drop channel">×</span>`;
            Chip.querySelector(".cc-x").onclick = (Event) => {
                Event.stopPropagation();
                this.Apply("channel", { Token: Layer.Token, Channel: Panel.Key, On: false });
            };
            Chips.appendChild(Chip);
        }
        Body.appendChild(Chips);

        const Idle = CHANNEL_PANELS.filter((P) => !PaintsChannel(Layer, P.Key));
        if (Idle.length > 0)
        {
            const Pool = document.createElement("div");
            Pool.className = "chan-pool";
            for (const Panel of Idle)
            {
                const Option = document.createElement("div");
                Option.className = "chan-pool-opt";
                Option.textContent = Panel.Label;
                Option.onclick = (Event) => {
                    Event.stopPropagation();
                    this.Apply("channel", { Token: Layer.Token, Channel: Panel.Key, On: true });
                };
                Pool.appendChild(Option);
            }
            Body.appendChild(Pool);
        }

        Clip.appendChild(Body);
        Shell.appendChild(Clip);
        Card.appendChild(Head);
        Card.appendChild(Shell);
        return Card;
    }

    BuildChannelPanel(Layer, Panel)
    {
        const Key       = `${Layer.Token}:${Panel.Key}`;
        const Collapsed = this.Collapsed.has(Key);

        const Host = document.createElement("div");
        Host.className = "chan-panel" + (Collapsed ? " collapsed" : "");

        const Head = document.createElement("div");
        Head.className = "chan-head";
        Head.innerHTML =
            `<span class="ch-tw">${Icon("chevronDown", 9)}</span>` +
            `<span class="ch-dot" style="background:${Hue(Layer.Classification)}"></span>` +
            `<span class="ch-title">${Panel.Label}</span>`;

        const Shell = document.createElement("div"); Shell.className = "chan-shell";
        const Clip  = document.createElement("div"); Clip.className  = "chan-clip";
        const Body  = document.createElement("div"); Body.className  = "chan-body";

        this.BuildChannelBody(Body, Layer, Panel);

        Clip.appendChild(Body);
        Shell.appendChild(Clip);

        Head.onclick = () => {
            if (this.Collapsed.has(Key)) { this.Collapsed.delete(Key); }
            else                          { this.Collapsed.add(Key); }
            Host.classList.toggle("collapsed", this.Collapsed.has(Key));
        };

        Host.appendChild(Head);
        Host.appendChild(Shell);
        return Host;
    }

    BuildChannelBody(Body, Layer, Panel)
    {
        // 🔴 The normal channel has NO editable value. It is derived from the painted height by central
        //    differences in the shader, so there is nothing to author here — and offering a colour field
        //    would let a brush write raw RGB into a normal, which is neither unit-length nor in tangent
        //    space and shades as coloured noise.
        if (Panel.Edit === "derived")
        {
            const Note = document.createElement("div");
            Note.className = "chan-note";
            Note.textContent = "Derived from the painted height. No value to author.";
            Body.appendChild(Note);
            return;
        }

        // ---- the channel's SOURCE -------------------------------------------------------------------
        // 🔴 The mode gates what follows, so it is built first and the body below is a consequence of it.
        //    A flat value, a painted texture and a procedural pass are three different owners of the same
        //    channel, and showing a value slider while the channel is actually driven by a generator is the
        //    panel lying about what the surface shows.
        const Mode = Layer.Modes?.[Panel.Key] ?? "Value";
        Body.appendChild(PropertyRow("Source", this.BuildModePicker(Layer, Panel, Mode)));

        // Texture mode is paint-driven: the atlas is authored by strokes, so there is no field to edit and
        // no generator to configure. Only paintable layers can actually receive those strokes.
        if (Mode === "Texture")
        {
            const Note = document.createElement("div");
            Note.className = "chan-note";
            Note.textContent = Layer.Paintable
                ? "Painted. Strokes on this layer author the channel; storage is allocated on first use."
                : "Texture source, but this layer kind does not accept strokes.";
            Body.appendChild(Note);
            Body.appendChild(this.BuildChannelPreview(Layer, Panel));
            return;
        }

        // Generator mode: the recipe owns the channel. Its parameters live on the layer, not the channel,
        // so they are edited once in the identity card rather than repeated under all three channels.
        if (Mode === "Generator")
        {
            const Note = document.createElement("div");
            Note.className = "chan-note";
            Note.textContent = Layer.Generator
                ? `Driven by the "${Layer.Generator}" generator. Its parameters are on the layer.`
                : "Generator source, but this layer has no generator recipe assigned.";
            Body.appendChild(Note);
            Body.appendChild(this.BuildChannelPreview(Layer, Panel));
            return;
        }

        if (Panel.Edit === "colour")
        {
            const Current = Array.isArray(Layer.Values[Panel.Key])
                ? ColourToHex(Layer.Values[Panel.Key]) : "#808080";
            Body.appendChild(PropertyRow("Colour", BuildColourField(Current, (Hex, Live) => {
                this.Commands("value", { Token: Layer.Token, Channel: Panel.Key, Value: HexToColour(Hex) });
                this.OnChange();
                if (!Live) { this.Refresh(); }
            })));
            return;
        }

        const Value = Number(Layer.Values[Panel.Key] ?? 0);
        Body.appendChild(PropertyRow("Amount", BuildSlider({
            Min: Panel.Min, Max: Panel.Max, Step: Panel.Step, Value: Value,
            OnInput: (Next, Live) => {
                this.Commands("value", { Token: Layer.Token, Channel: Panel.Key, Value: Next });
                this.OnChange();
                if (!Live) { this.Refresh(); }
            }
        })));
    }

    //--------------------------------------------------------------------------------------------------
    //                                      CHANNEL PREVIEW
    //--------------------------------------------------------------------------------------------------

    // The live thumbnail of what this channel actually holds, read back from its atlas.
    //
    // 🔴 Returns the element SYNCHRONOUSLY and fills the image in later. RenderChannels builds the whole
    //    pane in one pass and appends as it goes; awaiting a GPU readback per channel there would make the
    //    panel appear a channel at a time, and would make Refresh() async — which every caller, including
    //    LayerCommand, invokes without awaiting.
    BuildChannelPreview(Layer, Panel)
    {
        const Host = document.createElement("div");
        Host.className = "chan-preview";

        const Tile = document.createElement("div");
        Tile.className = "cp-tile";

        const Note = document.createElement("div");
        Note.className = "cp-note";
        Note.textContent = "Reading…";

        Host.appendChild(Tile);
        Host.appendChild(Note);

        // 🔴 The generation counter is what makes a late capture safe. The panel is rebuilt on every
        //    Refresh(), so by the time an await resolves, THIS element may already have been discarded and
        //    replaced — and the layer may no longer even be focused. Without the guard, a slow readback for
        //    the previously focused layer lands in the newly built panel and shows the wrong channel's
        //    content, which is indistinguishable from a preview that simply renders the wrong thing.
        const Generation = this.PreviewGeneration ?? 0;

        this.Capture(Layer.Token, Panel.Key).then((Preview) => {
            if ((this.PreviewGeneration ?? 0) !== Generation) { return; }
            if (!Tile.isConnected) { return; }

            if (!Preview)
            {
                // Not a failure: a lazily-allocated channel that has never been written has no atlas, and
                // saying so is more use than an empty tile that looks like a broken image.
                Tile.classList.add("cp-empty");
                Note.textContent = (Layer.Modes?.[Panel.Key] === "Texture" && Layer.Paintable)
                    ? "Not painted yet — no storage allocated."
                    : "No content for this channel yet.";
                return;
            }

            Tile.style.backgroundImage = `url(${Preview.Image})`;
            Note.textContent = `${Preview.Extent}² from the ${Preview.Atlas} atlas`;
        });

        return Host;
    }

    //--------------------------------------------------------------------------------------------------
    //                                      CHANNEL SOURCE
    //--------------------------------------------------------------------------------------------------

    // The Value / Texture / Generator segmented control for one channel.
    //
    // 🔴 The options come from the engine's CHANNEL_MODES, not from a literal list here. A hard-coded triple
    //    would keep rendering three buttons after the engine's vocabulary changed, and the extra one would
    //    send a mode string the "mode" verb silently ignores — the button would latch visually and do nothing.
    BuildModePicker(Layer, Panel, Current)
    {
        const Host = document.createElement("div");
        Host.className = "segment";

        for (const Name of CHANNEL_MODES)
        {
            const Option = document.createElement("div");
            Option.className = "seg-opt" + (Name === Current ? " sel" : "");
            Option.textContent = Name;

            // 🔴 Generator is offered ONLY where a recipe exists to drive the channel. Selecting it on a
            //    plain paint layer would set a mode nothing implements: the channel would stop accepting
            //    strokes and no pass would write it, so it would read as a dead channel with no way back
            //    except re-picking Value. Disabled-with-a-reason beats a lever that breaks the layer.
            const Allowed = (Name !== "Generator") || Boolean(Layer.Generator);
            if (!Allowed)
            {
                Option.classList.add("off");
                Option.title = "This layer has no generator recipe.";
            }

            Option.onclick = () => {
                if (!Allowed || Name === Current) { return; }
                this.Apply("mode", { Token: Layer.Token, Channel: Panel.Key, Mode: Name });
            };

            Host.appendChild(Option);
        }

        return Host;
    }

    //--------------------------------------------------------------------------------------------------
    //                                        DROPDOWN
    //--------------------------------------------------------------------------------------------------

    CloseLists()
    {
        if (!this.OpenList) { return; }
        this.OpenList.classList.remove("open");
        this.OpenList = null;

        // The add-layer list is a body-appended node, not a child of a persistent .dropdown, so closing it
        // means removing it outright — dropping the class alone would leave an orphan behind the card.
        if (this.AddLayerList) { this.AddLayerList.remove(); this.AddLayerList = null; }
    }

    // 📝 The list is position:fixed and placed by script, so it escapes the scrolling pane rather than
    //    being clipped by it.
    BuildDropdown(Options, Current, OnPick)
    {
        const Host = document.createElement("div");
        Host.className = "dropdown";

        const Head = document.createElement("div");
        Head.className = "dd-head";
        Head.innerHTML = `<span class="cur">${Current}</span>` +
            `<span class="caret">${Icon("chevronDown", 10)}</span>`;

        const List = document.createElement("div");
        List.className = "dd-list";
        for (const Option of Options)
        {
            const Item = document.createElement("div");
            Item.className = "dd-item" + (Option === Current ? " sel" : "");
            Item.innerHTML = `<span>${Option}</span><span class="radio"></span>`;
            Item.onclick = (Event) => {
                Event.stopPropagation();
                this.CloseLists();
                if (Option !== Current) { OnPick(Option); }
            };
            List.appendChild(Item);
        }

        Head.onclick = (Event) => {
            Event.stopPropagation();
            const WasOpen = Host.classList.contains("open");
            this.CloseLists();
            if (WasOpen) { return; }
            Host.classList.add("open");
            this.OpenList = Host;

            const Box = Head.getBoundingClientRect();
            List.style.left  = `${Box.left}px`;
            List.style.width = `${Math.max(Box.width, 128)}px`;
            // Flip above when the list would run past the bottom edge.
            const Tall = Math.min(280, Options.length * 29 + 8);
            List.style.top = `${Box.bottom + Tall > window.innerHeight ? Box.top - Tall - 3 : Box.bottom + 3}px`;
        };

        Host.appendChild(Head);
        Host.appendChild(List);
        return Host;
    }

    Release()
    {
        document.removeEventListener("pointerdown", this.CloseListOnOutside, true);
        this.Root.remove();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FIELD WIDGETS
//------------------------------------------------------------------------------------------------------------------------

function SectionLabel(Text, Glyph, Tail)
{
    const Element = document.createElement("div");
    Element.className = "meta-sect";
    Element.innerHTML = (Glyph ? `<span>${Icon(Glyph, 12)}</span>` : "") + `<span>${Text}</span>` +
        (Tail ? `<span class="ms-tail">${Tail}</span>` : "");
    return Element;
}

function PropertyRow(Label, Field)
{
    const Row = document.createElement("div");
    Row.className = "prow";
    const Tag = document.createElement("div");
    Tag.className = "plabel";
    Tag.textContent = Label;
    Row.appendChild(Tag);
    Row.appendChild(Field);
    return Row;
}

function BuildSwitch(On, OnFlip)
{
    const Host = document.createElement("div");
    Host.className = "switch" + (On ? " on" : "");
    Host.innerHTML = '<span class="nub"></span>';
    Host.onclick = () => {
        On = !On;
        Host.classList.toggle("on", On);
        OnFlip(On);
    };
    return Host;
}

// A numeric field plus a track.
//
// 📝 `OnInput` is called with (Value, Live). Live is true while the pointer is down; the caller uses it
//    to write the value without rebuilding the pane, and rebuilds once on release.
function BuildSlider(Spec)
{
    const Host  = document.createElement("div"); Host.className  = "sliderrow";
    const Box   = document.createElement("div"); Box.className   = "valuebox";
    const Num   = document.createElement("div"); Num.className   = "num";
    const Input = document.createElement("input"); Input.type    = "text";

    Num.appendChild(Input);
    Box.appendChild(Num);
    if (Spec.Unit)
    {
        const Unit = document.createElement("span");
        Unit.className = "unitseg";
        Unit.textContent = Spec.Unit;
        Box.appendChild(Unit);
    }

    const Track = document.createElement("div"); Track.className = "slider";
    const Fill  = document.createElement("div"); Fill.className  = "fill";
    const Knob  = document.createElement("div"); Knob.className  = "knob";
    Track.appendChild(Fill);
    Track.appendChild(Knob);
    Host.appendChild(Box);
    Host.appendChild(Track);

    const Places = DecimalsFor(Spec.Step);
    let   Value  = Spec.Value;

    const Paint = () => {
        const Percent = ((Value - Spec.Min) / (Spec.Max - Spec.Min)) * 100;
        Fill.style.width = `${Percent}%`;
        Knob.style.left  = `${Percent}%`;
        Input.value = Value.toFixed(Places);
    };
    const Settle = (Next, Live) => {
        const Clamped   = Math.max(Spec.Min, Math.min(Spec.Max, Next));
        const Quantised = Math.round(Clamped / Spec.Step) * Spec.Step;
        Value = Number(Quantised.toFixed(Places));
        Paint();
        Spec.OnInput(Value, Live);
    };
    const FromPointer = (Event) => {
        const Box2  = Track.getBoundingClientRect();
        const Ratio = Math.max(0, Math.min(1, (Event.clientX - Box2.left) / Box2.width));
        Settle(Spec.Min + Ratio * (Spec.Max - Spec.Min), true);
    };

    Track.onpointerdown = (Event) => {
        Event.preventDefault();
        Track.setPointerCapture(Event.pointerId);
        FromPointer(Event);
        const Move = (Motion) => FromPointer(Motion);
        const Up = () => {
            Track.removeEventListener("pointermove", Move);
            Track.removeEventListener("pointerup", Up);
            Spec.OnInput(Value, false);
        };
        Track.addEventListener("pointermove", Move);
        Track.addEventListener("pointerup", Up);
    };
    Input.onchange = () => {
        const Parsed = parseFloat(Input.value);
        if (Number.isFinite(Parsed)) { Settle(Parsed, false); } else { Paint(); }
    };
    // 📝 Swallowed so typing a value does not also fire the page's single-key shortcuts.
    Input.onkeydown = (Event) => {
        Event.stopPropagation();
        if (Event.key === "Enter") { Input.blur(); }
    };

    Paint();
    Host.ReadBack = (Next) => { Value = Next; Paint(); };
    return Host;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   COLOUR FIELD
//------------------------------------------------------------------------------------------------------------------------

function HexToHsv(Hex)
{
    const Packed = parseInt(Hex.slice(1), 16);
    const Red   = ((Packed >> 16) & 255) / 255;
    const Green = ((Packed >>  8) & 255) / 255;
    const Blue  = ( Packed        & 255) / 255;
    const Max = Math.max(Red, Green, Blue);
    const Min = Math.min(Red, Green, Blue);
    const Delta = Max - Min;

    let HueAngle = 0;
    if (Delta > 0)
    {
        if      (Max === Red)   { HueAngle = ((Green - Blue) / Delta + (Green < Blue ? 6 : 0)) / 6; }
        else if (Max === Green) { HueAngle = ((Blue - Red)   / Delta + 2) / 6; }
        else                    { HueAngle = ((Red - Green)  / Delta + 4) / 6; }
    }
    return { Hue: HueAngle * 360, Saturation: Max ? Delta / Max : 0, Value: Max };
}

function HsvToHex(HueAngle, Saturation, Value)
{
    const Component = (N) => {
        const K = (N + HueAngle / 60) % 6;
        return Math.round((Value - Value * Saturation * Math.max(0, Math.min(K, 4 - K, 1))) * 255);
    };
    return "#" + [Component(5), Component(3), Component(1)]
        .map((X) => X.toString(16).padStart(2, "0")).join("");
}

// A bar that opens an inline saturation/value box, a hue rail and a hex field.
//
// 🔴 `OnPick` is NOT called during construction. The field is built while rendering the pane, and a
//    construction-time call would write the layer's own current value straight back through the command
//    surface — bumping the revision and re-entering the render that is still running.
function BuildColourField(Current, OnPick)
{
    const Host = document.createElement("div");

    const Bar = document.createElement("div");
    Bar.className = "colorbar";
    Bar.innerHTML = `<span class="chip"></span><span class="cname"></span>` +
        `<span class="caret">▾</span>`;

    const Picker = document.createElement("div");
    Picker.className = "picker";
    Picker.innerHTML =
        `<div class="svbox"><span class="svknob"></span></div>` +
        `<div class="barstack"><div class="cbar huebar"><span class="cknob"></span></div></div>` +
        `<div class="hexrow"><div class="valuebox"><div class="num">` +
          `<input type="text" spellcheck="false"></div></div></div>`;

    Host.appendChild(Bar);
    Host.appendChild(Picker);

    const Chip  = Bar.querySelector(".chip");
    const Label = Bar.querySelector(".cname");
    const Plane = Picker.querySelector(".svbox");
    const Dot   = Picker.querySelector(".svknob");
    const Rail  = Picker.querySelector(".huebar");
    const Slide = Picker.querySelector(".cknob");
    const Input = Picker.querySelector("input");

    let State = HexToHsv(Current);

    const Paint = (Live, Notify) => {
        const Hex = HsvToHex(State.Hue, State.Saturation, State.Value);
        Plane.style.background =
            `linear-gradient(to top, #000, transparent), ` +
            `linear-gradient(to right, #fff, hsl(${State.Hue}, 100%, 50%))`;
        Dot.style.left    = `${State.Saturation * 100}%`;
        Dot.style.top     = `${(1 - State.Value) * 100}%`;
        Slide.style.left  = `${(State.Hue / 360) * 100}%`;
        Chip.style.background = Hex;
        Label.textContent = Hex.toUpperCase();
        Input.value       = Hex.toUpperCase();
        if (Notify) { OnPick(Hex, Live); }
    };

    const Scrub = (Surface, Apply) => {
        Surface.onpointerdown = (Event) => {
            Event.preventDefault();
            Event.stopPropagation();
            Surface.setPointerCapture(Event.pointerId);
            const Read = (Motion) => {
                const Box = Surface.getBoundingClientRect();
                Apply(Math.max(0, Math.min(1, (Motion.clientX - Box.left) / Box.width)),
                      Math.max(0, Math.min(1, (Motion.clientY - Box.top)  / Box.height)));
                Paint(true, true);
            };
            Read(Event);
            const Move = (Motion) => Read(Motion);
            const Up = () => {
                Surface.removeEventListener("pointermove", Move);
                Surface.removeEventListener("pointerup", Up);
                Paint(false, true);
            };
            Surface.addEventListener("pointermove", Move);
            Surface.addEventListener("pointerup", Up);
        };
    };

    Scrub(Plane, (X, Y) => { State.Saturation = X; State.Value = 1 - Y; });
    Scrub(Rail,  (X)    => { State.Hue = X * 360; });

    Bar.onclick = (Event) => { Event.stopPropagation(); Picker.classList.toggle("open"); };

    Input.onkeydown = (Event) => {
        Event.stopPropagation();
        if (Event.key === "Enter") { Input.blur(); }
    };
    Input.onchange = () => {
        const Text = Input.value.trim();
        if (/^#?[0-9a-fA-F]{6}$/.test(Text))
        {
            State = HexToHsv(Text.startsWith("#") ? Text : `#${Text}`);
        }
        Paint(false, true);
    };

    Paint(true, false);
    return Host;
}
