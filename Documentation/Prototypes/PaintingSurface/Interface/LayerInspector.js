/*====================================================================================================================================
                                                     LAYERINSPECTOR.JS
====================================================================================================================================*/
// 🧩 Tab-summoned layer manager: [stack | layer properties] ⇄ [identity | channels], driving the live stack

import { BLEND_MODES, LayerCapacity,
         MASK_COMPONENT_TYPES, MakeMask, MakeMaskComponent } from "../Layers/LayerStack.js";
import { CHANNEL_ORDER, CHANNEL_LABEL, CHANNEL_SLOTS } from "../Layers/ChannelSet.js";
import { CHANNEL_MODES, KindLabel, KindTint } from "../Layers/LayerKinds.js";

// 🔴 Labels and tints come from LAYER_KINDS, never from the legacy CLASSIFICATION_* tables. Those are keyed
//    by the DEAD vocabulary ("brushwork"/"flood"), while Layer.Classification is only an alias of Layer.Kind
//    and so now carries "paint"/"fill". Looking a live kind up in the legacy table returns undefined, which
//    is exactly what rendered as "Paint 1 undefined" in the properties header. KindLabel/KindTint fall back
//    to the raw kind and a neutral grey instead of injecting the string "undefined" into the UI.
const LabelOf = (Layer) => KindLabel(Layer.Kind ?? Layer.Classification);

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
    // 🔴 Keyed "paint"/"fill", the LIVE kinds — not the legacy "brushwork"/"flood". Under the old keys these
    //    two never matched a real layer, so every paint and fill row silently drew an EMPTY icon: the lookup
    //    missed, the ternary below substituted "", and a blank square is indistinguishable from art that
    //    simply has no fill.
    paint: (H) => `
        <path d="M14.6 3.6 L20.4 9.4 L11 18.8 L5.2 13 Z" fill="${H}"/>
        <path d="M5.2 13 L11 18.8 L8.4 21.4 L3.2 21.4 L2.6 16.2 Z" fill="${Shade(H, .4)}"/>
        <path d="M14.6 3.6 L20.4 9.4 L17.8 12 L12 6.2 Z" fill="${Shade(H, .6)}"/>`,
    fill: (H) => `
        <path d="M11.4 2.8 L20.6 12 L12.3 20.3 L3.1 11.1 Z" fill="${H}"/>
        <path d="M11.4 2.8 L20.6 12 L12.3 20.3 L11.4 19.4 L11.4 2.8 Z" fill="${Shade(H, .42)}"/>
        <path d="M3.1 11.1 L12.3 20.3 L12.3 14.4 L6.2 14.4 Z" fill="${Shade(H, .62)}"/>`
};

const Hue = (Kind) => KindTint(Kind);

const ClassificationSvg = (Kind, Size) =>
    SvgWrap(CLASSIFICATION_ART[Kind] ? CLASSIFICATION_ART[Kind](Hue(Kind)) : "", Size);

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
    image:       `<rect ${Stroked} x="3.5" y="4.5" width="17" height="15" rx="2.5"/><circle ${Stroked} cx="9" cy="9.8" r="1.7"/><path ${Stroked} d="M4 16.5 L9.5 12.5 L14 16 L17 13.5 L20.5 16.5"/>`,
    cube:        `<path ${Stroked} d="M12 3 L20.5 7.5 V16.5 L12 21 L3.5 16.5 V7.5 Z"/><path ${Stroked} d="M3.5 7.5 L12 12 L20.5 7.5 M12 12 V21"/>`,
    bucket:      `<path ${Stroked} d="M11 3 L20 12 L12 20 L3 11 Z"/><path ${Stroked} d="M18 16.5 c1.6 2.2 2.4 3.5 2.4 4.3 a2.4 2.4 0 0 1 -4.8 0 c0 -0.8 0.8 -2.1 2.4 -4.3 Z"/>`,
    palette:     `<path ${Stroked} d="M12 3.2 a8.8 8.8 0 0 0 0 17.6 c1.6 0 2.2 -1 2.2 -2 0 -1.3 -1.1 -1.8 -1.1 -3 0 -1.1 0.9 -2 2 -2 H18 a3.2 3.2 0 0 0 3.2 -3.2 C21.2 6.6 17 3.2 12 3.2 Z"/><circle cx="8.2" cy="9.4" r="1.25" fill="currentColor"/><circle cx="12" cy="7.6" r="1.25" fill="currentColor"/><circle cx="7.4" cy="14" r="1.25" fill="currentColor"/>`,
    sparkle:     `<path ${Stroked} d="M10 3.4 L11.7 8.3 L16.6 10 L11.7 11.7 L10 16.6 L8.3 11.7 L3.4 10 L8.3 8.3 Z"/><path ${Stroked} d="M17.4 14.2 L18.3 16.7 L20.8 17.6 L18.3 18.5 L17.4 21 L16.5 18.5 L14 17.6 L16.5 16.7 Z"/>`,
    brush:       `<path ${Stroked} d="M17.6 3.9 a2.3 2.3 0 0 1 3.2 3.2 L12.4 15.6 L9.1 12.3 Z"/><path ${Stroked} d="M9.1 12.3 L12.4 15.6 c0 2.3 -1.9 4.2 -4.2 4.2 H3.4 c1.7 -0.9 1.5 -2.4 1.5 -3.7 a3.7 3.7 0 0 1 4.2 -3.8 Z"/>`,
    // 📝 Stand-ins for the reference's Lucide set, drawn in this file's own inline idiom: `layers` for the
    //    add menu's footer, `mask` for venetian-mask, `circle`/`flip` for the mask fill and invert tools,
    //    and `close` for the component remove button.
    layers:      `<path ${Stroked} d="M12 3 L21 8 L12 13 L3 8 Z"/><path ${Stroked} d="M3 13 L12 18 L21 13"/>`,
    mask:        `<path ${Stroked} d="M3.2 6.4 h17.6 v4.4 a9.4 9.4 0 0 1 -8.8 9.4 a9.4 9.4 0 0 1 -8.8 -9.4 Z"/><circle cx="8.4" cy="11.4" r="1.5" fill="currentColor"/><circle cx="15.6" cy="11.4" r="1.5" fill="currentColor"/>`,
    circle:      `<circle ${Stroked} cx="12" cy="12" r="8"/>`,
    flip:        `<path ${Stroked} d="M3.5 12 H20.5"/><path ${Stroked} d="M12 3.5 L16.5 9 H7.5 Z"/><path ${Stroked} d="M12 20.5 L7.5 15 H16.5 Z"/>`,
    close:       `<path ${Stroked} d="M6 6 L18 18 M18 6 L6 18"/>`
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

//------------------------------------------------------------------------------------------------------------------------
//                                                     ADD LAYER BAR
//------------------------------------------------------------------------------------------------------------------------

// The four layer types the Add Layer bar offers, ported from Studio-standalone.html's LAYER_TYPES +
// LAYER_TYPE_DESC — including its names, its descriptions, its glyphs and its tag hues.
//
// 🔴 `Label` is the reference's user-facing word and `Kind` is the ENGINE kind, and they are separate fields
//    because the two vocabularies genuinely disagree. `Layer.Classification` is only an ALIAS of `Layer.Kind`
//    (LayerStack.js), so passing a classification through as the kind sets Kind to a string that is NOT a key
//    in LAYER_KINDS. IsPaintable() then returns false and the stroke path swallows every dab with no error —
//    an earlier revision shipped "Brushwork"/"Flood" that way, and Flood silently became a paint layer rather
//    than a fill, arriving empty where uniform coverage was asked for. Naming the kind explicitly is the fix.
//
// 📝 The reference's own names are Material / Generator / Paint / Fill. The invented "Brushwork"/"Flood" are
//    gone: Paint is the kind that takes strokes, Fill is the flooded one.
const LAYER_TYPES = [
    { Label: "Material",  Kind: "material",  Glyph: "cube",    Tag: "#8b5cf6",
      Note: "Full PBR surface with every channel.", Preset: "plastic" },
    { Label: "Generator", Kind: "generator", Glyph: "sparkle", Tag: "#10b981",
      Note: "Procedural mask-driven wear & grime.", Generator: "rust" },
    { Label: "Paint",     Kind: "paint",     Glyph: "brush",   Tag: "#f97316",
      Note: "Hand-painted brush strokes." },
    { Label: "Fill",      Kind: "fill",      Glyph: "bucket",  Tag: "#3b82f6",
      Note: "Flat fill across the surface." }
];

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

//------------------------------------------------------------------------------------------------------------------------
//                                                     MASK SWATCH
//------------------------------------------------------------------------------------------------------------------------

// A tiny greyscale gradient that reads as the mask's fill + invert state. Ported from maskSwatchStyle().
function MaskSwatchStyle(Mask)
{
    const Light = Mask.Fill === "white" ? "#f0f0f0" : "#101010";
    const Dark  = Mask.Fill === "white" ? "#9a9a9a" : "#2a2a2a";
    return Mask.Invert
        ? `linear-gradient(135deg,${Dark},${Light})`
        : `linear-gradient(135deg,${Light},${Dark})`;
}

const MaskMeta = (Mask) =>
    `${Mask.Fill === "white" ? "White" : "Black"} fill` +
    `${Mask.Invert ? " · inverted" : ""} · ${Mask.Components.length} comp`;

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
          <!-- 🔴 The Add Layer bar is its OWN band, a sibling of the stack rather than a control inside the
                  pane head — the layer stack is separate from Add Layer, exactly as the reference has it.
                  It sits above the filter so the control that grows the stack is never scrolled away or
                  hidden behind a filter term that matches nothing. -->
          <div class="ls-addbar" data-part="AddBar">
            <button class="ls-addmain" data-part="AddButton" aria-haspopup="menu"></button>
            <div class="ls-addmenu" data-part="AddMenu" role="menu"></div>
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

// 🔴 These MUST match .summon-menu's width/height in LayerInspector.css. Show() clamps the card against the
//    viewport using these numbers, so if the CSS grows and these do not, the clamp reserves too little room
//    and the card is positioned partly off-screen — with the overflow hidden, the bottom rows simply cannot
//    be reached. Scaled 548×372 → 632×430 together with the stylesheet.
const MenuWidth  = 632;
const MenuHeight = 430;
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
        this.RowDragged = false;
        this.OpenList   = null;

        // Which row has its inline editor unfolded. Seeded to the focused layer so the card opens showing
        // the editor for the layer it is already pointed at, rather than requiring a click to reveal it.
        this.ExpandToken = Stack.FocusToken;

        this.AddOpen = false;

        this.Part.StackIcon.innerHTML   = Icon("stack", 18);
        // Ported verbatim from the reference's .ls-addmain: plus glyph, "Add Layer", rotating chevron.
        this.Part.AddButton.innerHTML   = `${Icon("plus", 14)} Add Layer ` +
                                          `<span class="la-chev">${Icon("chevronDown", 13)}</span>`;
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
        this.Part.AddButton.onclick        = (Event) => {
            Event.stopPropagation();
            const WasOpen = this.AddOpen;
            this.CloseLists();
            if (WasOpen) { return; }
            // 📝 Rebuilt on every open, not once at construction, so the capacity guard reflects the stack
            //    as it is right now.
            this.RenderAddMenu();
            // The reference toggles `open` on the BAR, and the menu and chevron are styled off that.
            this.Part.AddBar.classList.add("open");
            this.AddOpen = true;
        };
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
        // 🔴 Both menus are tested, not just OpenList. The add menu is not tracked by OpenList (it is
        //    anchored in the pane head rather than fixed-positioned), so a handler that only consulted
        //    OpenList would leave it standing open behind the next press.
        this.CloseListOnOutside = (Event) => {
            if (this.OpenList && !this.OpenList.contains(Event.target)) { this.CloseLists(); }
            if (this.AddOpen && !this.Part.AddBar.contains(Event.target)) { this.CloseAddMenu(); }
            if (this.MaskMenu && !this.MaskMenu.parentElement.contains(Event.target))
            {
                this.CloseMaskMenu();
            }
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
        if (this.OpenList || this.AddOpen) { this.CloseLists(); return true; }
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
        // 🔴 The expand FOLLOWS focus whenever focus moved somewhere this panel did not send it — a new
        //    layer from the add bar, or a delete reassigning focus. Without this the token would still name
        //    the previous layer, so the newly focused row would render folded and the just-added layer would
        //    look like it arrived without a mask editor. Deliberately collapsed rows keep their null.
        if (this.ExpandToken !== null && this.ExpandToken !== this.Stack.FocusToken)
        {
            this.ExpandToken = this.Stack.FocusToken;
        }

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

        // 🔴 The expand is appended as a SIBLING after the row, not nested inside it, matching the reference.
        //    Nesting it would make the row 200px tall, so the row's own hover/selection fill would cover the
        //    whole editor and every press inside the editor would also read as a press on the row.
        for (const Layer of Layers)
        {
            Body.appendChild(this.BuildRow(Layer));
            if (this.IsExpanded(Layer)) { Body.appendChild(this.BuildMaskExpand(Layer)); }
        }

        if (Body.children.length === 0)
        {
            Body.innerHTML = '<div class="empty-state">No layers match.<br>Clear the filter to see the stack.</div>';
        }

        this.Part.Tally.textContent = this.Stack.Count;

        const Hidden = this.Stack.Layers.filter((L) => !L.Shown).length;
        this.Part.StackFoot.innerHTML =
            `<span class="pf-strong">${this.Stack.Count}</span> layers` +
            (Hidden ? `<span class="pf-dot">·</span><span>${Hidden} hidden</span>` : "") +
            `<span class="pf-spacer"></span><span>${this.Stack.Count} / ${LayerCapacity}</span>`;
    }

    // Open or close a row's inline expand.
    //
    // 🔴 Expansion is tracked SEPARATELY from focus, unlike the reference, which collapses by setting
    //    selectedId = null. Here the focus token is also the paint target, and LayerStack refuses a stroke
    //    when focus is unset rather than redirecting it — so collapsing via focus would leave the user with
    //    a visible stack whose next brush stroke silently does nothing. Clicking a row therefore always
    //    focuses it, and only the second click on the already-open row folds the editor away.
    ToggleExpand(Layer)
    {
        if (Layer.Token !== this.Stack.FocusToken)
        {
            this.ExpandToken = Layer.Token;
            this.Apply("focus", { Token: Layer.Token });
            return;
        }

        this.ExpandToken = this.ExpandToken === Layer.Token ? null : Layer.Token;
        this.Refresh();
    }

    // A row shows its editor when it is both the focused layer and not explicitly collapsed.
    IsExpanded(Layer)
    {
        return Layer.Token === this.Stack.FocusToken && this.ExpandToken !== null &&
               this.ExpandToken === Layer.Token;
    }

    BuildRow(Layer)
    {
        const Row = document.createElement("div");
        Row.className = "stack-row" +
            (Layer.Token === this.Stack.FocusToken ? " active" : "") +
            (Layer.Shown ? "" : " layer-muted");
        Row.dataset.token = Layer.Token;

        // 📝 An explicit tag wins over the kind tint; untagged rows keep classifying themselves by kind.
        const Tint   = Layer.Tag ?? Hue(Layer.Classification);
        const Swatch = SwatchOf(Layer);
        // The authored colour is only the PLACEHOLDER now — the real painted atlas replaces it below, once
        // the readback lands. A layer with no storage keeps the placeholder, which is the honest answer.
        const Thumb  = Swatch
            ? `<span class="sr-thumb-fill" style="background:${Swatch}"></span>`
            : ClassificationSvg(Layer.Classification, 15);

        const Open = this.IsExpanded(Layer);
        const Mask = Layer.Mask;

        // 📝 `expanded` is what lets the row and its editor render as ONE card: the row drops its bottom
        //    radius and border, the expand drops its top ones, and the seam between the two closes up.
        if (Open) { Row.classList.add("expanded"); }

        // 📝 The badge appears only once a mask actually exists, so an unmasked row stays uncluttered and the
        //    badge's presence is itself the signal that the layer is masked.
        const MaskBadge = Mask?.Enabled
            ? `<span class="sr-mask active" title="Mask · ${Mask.Fill}` +
              `${Mask.Invert ? " · inverted" : ""} · ${Mask.Components.length} comp" ` +
              `style="background:${MaskSwatchStyle(Mask)}"></span>`
            : "";

        Row.innerHTML =
            `<span class="sr-twisty ${Open ? "open" : "closed"}">${Icon("chevronDown", 13)}</span>` +
            `<span class="sr-tag" style="background:${Tint}"></span>` +
            `<span class="sr-thumb">${Thumb}</span>` +
            `<span class="sr-text">` +
              `<span class="sr-name"></span>` +
              `<span class="sr-meta">${LabelOf(Layer)} · ${Layer.Blend} · ` +
                `${ChannelsOf(Layer).length} ch</span>` +
            `</span>` +
            MaskBadge +
            `<span class="sr-opacity" title="Drag to adjust opacity">${Layer.Opacity}%</span>` +
            `<span class="sr-visibility" title="${Layer.Shown ? "Hide" : "Show"}">` +
              `${Icon(Layer.Shown ? "eyeOpen" : "eyeOff", 14)}</span>`;

        // 🔴 The name is assigned as TEXT, not interpolated into the markup above. A layer name is
        //    user-typed, and a name containing "<" would otherwise be parsed as markup — at best the
        //    name vanishes, at worst the row's own handlers are replaced by injected ones.
        Row.querySelector(".sr-name").textContent = Layer.Name;

        // The thumbnail shows the layer's actual painted colour atlas, not its authored value.
        this.FillFromAtlas(Row.querySelector(".sr-thumb"), Layer, "baseColour");

        Row.onclick = (Event) => {
            if (this.RowDragged) { this.RowDragged = false; return; }
            if (Event.target.closest(".sr-visibility") || Event.target.closest(".sr-opacity") ||
                Event.target.closest(".sr-mask")) { return; }
            this.ToggleExpand(Layer);
        };
        Row.ondblclick = (Event) => {
            if (Event.target.closest(".sr-visibility") || Event.target.closest(".sr-opacity") ||
                Event.target.closest(".sr-mask")) { return; }
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
        // The twisty is the explicit disclosure control, so it toggles without waiting for the row's
        // own handler — and it stops propagation so the row does not then toggle it straight back.
        Row.querySelector(".sr-twisty").onclick = (Event) => {
            Event.stopPropagation();
            this.ToggleExpand(Layer);
        };
        this.BindOpacityDrag(Row.querySelector(".sr-opacity"), Layer);
        if (!this.FilterTerm) { this.BindRowDrag(Row, Layer); }

        return Row;
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
            if (Event.target.closest(".sr-visibility") || Event.target.closest(".sr-opacity") ||
                Event.target.closest(".sr-mask")) { return; }

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

    //--------------------------------------------------------------------------------------------------
    //                                      LAYER MASK
    //--------------------------------------------------------------------------------------------------

    // The inline panel beneath the focused row, ported from the reference's buildLayerExpand/buildMaskEditor:
    // Visible, a rule, Blend / Opacity / Resolution, a rule, then the Mask block.
    //
    // 🔴 These four live HERE and nowhere else. They were also in the properties pane, and two controls over
    //    one value is not a convenience — each rebuilds the panel the other is drawn in, so moving the pane's
    //    Opacity slider re-renders the expand's slider out from under a finger already on it. The pane keeps
    //    Preview, Channels and Actions; the row's own expand owns the four per-layer settings.
    BuildMaskExpand(Layer)
    {
        const Box = document.createElement("div");
        Box.className = "ls-expand";

        // A group wrapper for the switch, matching the reference's .lse-grp.
        const Group = document.createElement("div");
        Group.className = "lse-grp";
        Group.appendChild(SwitchRow("Visible", Layer.Shown, (On) =>
            this.Apply("show", { Token: Layer.Token, Shown: On })));
        Box.appendChild(Group);

        Box.appendChild(Rule());

        Box.appendChild(Control("Blend mode", this.BuildDropdown(BLEND_MODES, Layer.Blend, (Pick) =>
            this.Apply("blend", { Token: Layer.Token, Blend: Pick }))));

        // 🔴 Live drags write through Commands, not Apply. Apply refreshes, and refreshing rebuilds this very
        //    expand — so the slider the pointer is captured on is destroyed mid-drag and the value freezes at
        //    wherever the first move landed. The row's opacity pill is nudged directly instead, and the full
        //    rebuild waits for release.
        Box.appendChild(Control("Opacity", BuildSlider({
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

        // 🔴 The options come from the STACK, which asks the device, rather than from a fixed list. The
        //    reference offers up to 8K unconditionally; that is exactly the default WebGPU limit, so a device
        //    reporting less would fail inside createTexture rather than in the menu that promised it.
        //
        // 📝 Labelled "1K"/"2K" but valued in texels, so what the dropdown shows and what the engine allocates
        //    cannot drift — the label is presentation only.
        const Options = this.Stack.ResolutionOptions ?? [];
        const Current = Options.find((O) => O.Value === Layer.Extent);
        Box.appendChild(Control("Resolution", this.BuildDropdown(
            Options.map((O) => O.Label), Current?.Label ?? `${Layer.Extent}`, (Pick) => {
                const Chosen = Options.find((O) => O.Label === Pick);
                if (Chosen) { this.Apply("resolution", { Token: Layer.Token, Extent: Chosen.Value }); }
            })));

        Box.appendChild(Rule());

        const Title = document.createElement("div");
        Title.className = "lse-title";
        Title.innerHTML = `${Icon("mask", 12)}<span>Mask</span>`;
        Box.appendChild(Title);
        Box.appendChild(this.BuildMaskEditor(Layer));

        return Box;
    }

    // Commit a mask edit and rebuild the panel around it.
    //
    // 🔴 The Touch is what keeps the stack revision honest. Without it the row would redraw while the
    //    compositor kept serving its cached pre-edit result, so the swatch and the model would disagree
    //    until some unrelated edit happened to invalidate the composite.
    //
    // 📝 Mask fields are still assigned by the callers rather than passed through the `mask` command verb.
    //    The verb exists for the harness and for anything driving the stack without this panel; routing the
    //    panel through it too would mean a full rebuild per pointer move during an opacity drag, which is
    //    exactly what the live/commit split below is avoiding.
    CommitMask()
    {
        this.Stack.Touch();
        this.Refresh();
        this.OnChange();
    }

    BuildMaskEditor(Layer)
    {
        const Mask = Layer.Mask ?? (Layer.Mask = MakeMask());
        const Wrap = document.createElement("div");

        // No mask yet: offer the one call-to-action and nothing else.
        if (!Mask.Enabled)
        {
            const Add = document.createElement("button");
            Add.className = "msk-empty";
            Add.innerHTML = `${Icon("mask", 14)} Add Mask`;
            Add.onclick = (Event) => {
                Event.stopPropagation();
                Mask.Enabled = true;
                this.CommitMask();
            };
            Wrap.appendChild(Add);
            return Wrap;
        }

        // ---- preview + summary ----------------------------------------------------------------------
        // 🔴 The PREVIEW is the paint target selector, exactly as in Substance: click the mask thumbnail
        //    and the brush paints the mask, click it again and it paints the layer's channels. No new UI
        //    is introduced for this — the thumbnail already existed and was inert, and adding a separate
        //    "paint mask" toggle beside it would give the panel two controls for one piece of state.
        const Row = document.createElement("div");
        Row.className = "msk-row" + (Mask.Target ? " targeting" : "");
        Row.innerHTML =
            `<div class="msk-prev${Mask.Target ? " targeting" : ""}" ` +
              `title="${Mask.Target ? "Painting the MASK — click to paint the layer" : "Click to paint this mask"}">` +
              `<div class="msk-chk"></div>` +
              `<div class="msk-grad" style="background:${MaskSwatchStyle(Mask)};` +
              `opacity:${Mask.Opacity / 100}"></div></div>` +
            `<div class="msk-info"><div class="msk-nm">Layer Mask</div>` +
              `<div class="msk-meta">${MaskMeta(Mask)}</div></div>`;

        // 🔴 Targeting is cleared on every OTHER layer, not just set on this one. The stroke router asks
        //    the focused layer whether its mask is targeted, so two layers both flagged is not directly
        //    harmful — but the flag drives the "you are painting a mask" affordance, and two layers
        //    claiming it at once makes the panel lie about where the next stroke lands.
        Row.querySelector(".msk-prev").onclick = (Event) => {
            Event.stopPropagation();
            const Next = !Mask.Target;
            for (const Other of this.Stack.Layers) { if (Other.Mask) { Other.Mask.Target = false; } }
            Mask.Target = Next;
            this.CommitMask();
        };
        Wrap.appendChild(Row);

        // ---- fill black / fill white / invert -------------------------------------------------------
        const Bar = document.createElement("div");
        Bar.className = "msk-toolbar";
        const Tool = (Label, Glyph, On, Run) => {
            const Button = document.createElement("button");
            Button.className = "msk-tool" + (On ? " on" : "");
            Button.innerHTML = `${Icon(Glyph, 13)} ${Label}`;
            Button.onclick = (Event) => { Event.stopPropagation(); Run(); };
            return Button;
        };
        Bar.appendChild(Tool("Black",  "circle", Mask.Fill === "black",
            () => { Mask.Fill = "black"; this.CommitMask(); }));
        Bar.appendChild(Tool("White",  "circle", Mask.Fill === "white",
            () => { Mask.Fill = "white"; this.CommitMask(); }));
        Bar.appendChild(Tool("Invert", "flip",   Mask.Invert,
            () => { Mask.Invert = !Mask.Invert; this.CommitMask(); }));
        Wrap.appendChild(Bar);

        // ---- mask opacity ---------------------------------------------------------------------------
        // 📝 The live drag repaints the gradient in place and only commits on release, the same contract the
        //    row's opacity pill uses — committing per pointer move would rebuild this editor mid-drag.
        Wrap.appendChild(PropertyRow("Opacity", BuildSlider({
            Min: 0, Max: 100, Step: 1, Value: Mask.Opacity, Unit: "%",
            OnInput: (Value, Live) => {
                Mask.Opacity = Value;
                const Gradient = Wrap.querySelector(".msk-grad");
                if (Gradient) { Gradient.style.opacity = Value / 100; }
                if (!Live) { this.CommitMask(); }
            }
        })));

        // ---- the component list ---------------------------------------------------------------------
        const List = document.createElement("div");
        List.className = "msk-comps";
        for (const Component of Mask.Components)
        {
            const Meta = MASK_COMPONENT_TYPES[Component.Type] ?? { Glyph: "cube" };
            const Item = document.createElement("div");
            Item.className = "msk-comp";
            Item.innerHTML =
                `<span class="mc-ico">${Icon(Meta.Glyph, 13)}</span>` +
                `<span class="mc-tx"><span class="mc-nm"></span>` +
                  `<span class="mc-md">${Component.Type}</span></span>` +
                `<button class="mc-x" title="Remove component">${Icon("close", 12)}</button>`;
            Item.querySelector(".mc-nm").textContent = Component.Name;
            Item.querySelector(".mc-x").onclick = (Event) => {
                Event.stopPropagation();
                Mask.Components = Mask.Components.filter((X) => X.Token !== Component.Token);
                this.CommitMask();
            };
            List.appendChild(Item);
        }
        Wrap.appendChild(List);

        // ---- add a component ------------------------------------------------------------------------
        const Add = document.createElement("button");
        Add.className = "msk-addcomp";
        Add.innerHTML = `${Icon("plus", 12)} Add component`;

        const Menu = document.createElement("div");
        Menu.className = "msk-addmenu";
        Menu.innerHTML = `<div class="cm-h">Mask Components</div>`;
        for (const [Type, Meta] of Object.entries(MASK_COMPONENT_TYPES))
        {
            const Option = document.createElement("button");
            Option.innerHTML = `${Icon(Meta.Glyph, 13)}<span>${Type}</span>`;
            Option.title = Meta.Note;
            Option.onclick = (Event) => {
                Event.stopPropagation();
                Mask.Components.push(MakeMaskComponent(Type));
                this.CloseMaskMenu();
                this.CommitMask();
            };
            Menu.appendChild(Option);
        }
        Add.appendChild(Menu);
        Add.onclick = (Event) => {
            Event.stopPropagation();
            const WasOpen = Menu.classList.contains("open");
            this.CloseLists();
            if (WasOpen) { return; }
            Menu.classList.add("open");
            this.MaskMenu = Menu;
        };
        Wrap.appendChild(Add);

        // ---- drop the mask entirely -----------------------------------------------------------------
        const Clear = document.createElement("button");
        Clear.className = "msk-tool";
        Clear.style.width = "100%";
        Clear.style.marginTop = "7px";
        Clear.innerHTML = `${Icon("trash", 13)} Remove mask`;
        Clear.onclick = (Event) => {
            Event.stopPropagation();
            // 🔴 Replaced with a FRESH mask rather than just clearing Enabled, matching the reference. Leaving
            //    the old components behind would resurrect every one of them the next time a mask was added.
            Layer.Mask = MakeMask();
            this.CommitMask();
        };
        Wrap.appendChild(Clear);

        return Wrap;
    }

    CloseMaskMenu()
    {
        if (!this.MaskMenu) { return; }
        this.MaskMenu.classList.remove("open");
        this.MaskMenu = null;
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
        this.Part.MetaClass.textContent = `${LabelOf(Layer)} layer`;
        this.Part.MetaIcon.innerHTML    = ClassificationSvg(Layer.Classification, 17);

        // 🔴 No name/class hero card here. It restated what the pane header directly above it already says
        //    (MetaName + MetaClass are the same two strings) beside a thumbnail of the same atlas the big
        //    preview below renders far larger. Three copies of one layer's identity stacked vertically, and
        //    the small thumbnail was the least legible of them — the big preview is sufficient.
        Body.appendChild(SectionLabel("Preview", "image"));
        Body.appendChild(this.BuildLayerPreview(Layer));

        Body.appendChild(SectionLabel("Properties", "sliders"));

        // 🔴 Visible / Blend / Opacity / Resolution are NOT here. They moved to the row's own inline expand
        //    (BuildMaskExpand), where the reference puts them and where they sit next to the layer they
        //    describe. They were in both places for a while, and two controls over one value meant each one
        //    rebuilt the panel the other lived in: dragging this pane's Opacity slider re-rendered the
        //    expand's, and vice versa, so whichever the pointer was captured on was destroyed mid-drag.
        //
        // 📝 What this pane keeps is what the expand has no room for: the large preview, the channel tally,
        //    and Delete. The footer below still REPORTS opacity and blend, read-only — reporting a value is
        //    not a second control over it.
        const ChannelRow = document.createElement("div");
        ChannelRow.className = "meta-row";
        ChannelRow.innerHTML = `<span class="mr-k">Channels</span>` +
            `<span class="mr-v">${ChannelsOf(Layer).length} / ${CHANNEL_PANELS.length} active</span>`;
        Body.appendChild(ChannelRow);

        // The layer's atlas size, reported here and AUTHORED in the expand. Same read-only contract as the
        // footer's opacity: the pane states facts, the expand changes them.
        const SizeRow = document.createElement("div");
        SizeRow.className = "meta-row";
        SizeRow.innerHTML = `<span class="mr-k">Resolution</span>` +
            `<span class="mr-v">${Layer.Extent}²${Layer.Allocated ? "" : " · unallocated"}</span>`;
        Body.appendChild(SizeRow);

        Body.appendChild(SectionLabel("Actions"));
        Body.appendChild(this.BuildActions(Layer));

        const Cta = document.createElement("div");
        Cta.className = "meta-cta";
        Cta.innerHTML = `<span>Channels</span>${Icon("chevron", 13)}<span class="cta-kbd">Tab</span>`;
        Cta.onclick = () => this.ShowChannels();
        Body.appendChild(Cta);

        this.Part.MetaFoot.innerHTML =
            `<span class="pf-hue" style="background:${Tint}"></span>` +
            `<span>${LabelOf(Layer)}</span>` +
            `<span class="pf-dot">·</span><span class="pf-strong">${Layer.Opacity}%</span>` +
            `<span class="pf-spacer"></span><span>${Layer.Blend}</span>`;
    }

    // Delete is the ONLY action this pane offers.
    //
    // 🔴 Raise / Lower and the four "Add <kind>" rows were deliberately removed, because each duplicated a
    //    control that already exists and reads better elsewhere: restacking is drag-to-reorder in the layer
    //    stack, and creating a layer is the Add Layer bar above it. A second copy of a verb is not a
    //    convenience — it is a second thing to keep in sync, and the add rows already caused exactly that
    //    bug once (they passed `Classification` where `Kind` was expected, silently making every Flood a
    //    paint layer). One entry point per verb is what stops that recurring.
    BuildActions(Layer)
    {
        const Host  = document.createElement("div");
        Host.className = "meta-actions";

        const Add = (Glyph, Label, Enabled, Run, Danger) => {
            const Item = document.createElement("div");
            Item.className = "act-item" + (Enabled ? "" : " disabled") + (Danger ? " danger" : "");
            Item.innerHTML = `<span class="ic">${Icon(Glyph, 15)}</span><span>${Label}</span>`;
            if (Enabled) { Item.onclick = Run; }
            Host.appendChild(Item);
        };

        // 🔴 The stack refuses to remove its last layer, so the row is disabled rather than offered and
        //    then silently ignored.
        Add("trash", "Delete", this.Stack.Count > 1,
            () => this.Apply("remove", { Token: Layer.Token }), true);

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
        // 📝 The same painted atlas the stack row and the hero show, so stepping between panes does not
        //    change what the layer appears to contain.
        this.FillFromAtlas(Chip, Layer, "baseColour");
        Body.appendChild(Chip);

        const Name = document.createElement("div");
        Name.className = "ident-name";
        Name.textContent = Layer.Name;
        Body.appendChild(Name);

        const Class = document.createElement("div");
        Class.className = "ident-class";
        Class.style.color = Tint;
        Class.textContent = LabelOf(Layer);
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
            `<span>${LabelOf(Layer)}</span>`;
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
            `${LabelOf(Layer)} · ${Layer.Blend}`;
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

        // 📝 Value mode gets a preview too, and it deliberately shows the FLAT authored value rather than the
        //    texels still sitting in the atlas. That is the point of the tile here: it confirms the channel is
        //    reading its value and not its paint. The strokes are untouched underneath — switching back to
        //    Texture brings them straight back — so this is the honest picture of what the surface renders now.
        if (Panel.Edit === "colour")
        {
            const Current = Array.isArray(Layer.Values[Panel.Key])
                ? ColourToHex(Layer.Values[Panel.Key]) : "#808080";
            Body.appendChild(PropertyRow("Colour", BuildColourField(Current, (Hex, Live) => {
                this.Commands("value", { Token: Layer.Token, Channel: Panel.Key, Value: HexToColour(Hex) });
                this.OnChange();
                if (!Live) { this.Refresh(); }
            })));
            Body.appendChild(this.BuildChannelPreview(Layer, Panel));
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
        Body.appendChild(this.BuildChannelPreview(Layer, Panel));
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
    // The layer's big preview: the painted Colour atlas at size, with the other channels as a strip beneath.
    //
    // 🔴 The hero is the COLOUR atlas, not a composite of all six channels, and the strip names each channel
    //    it shows. Six channels cannot honestly be flattened into one tile: metallic, roughness and height
    //    are three greyscale components sharing ONE Material atlas, and normal is derived and never stored.
    //    Packing them into a single RGB image would be a false-colour debug view, not a preview of the
    //    surface — a viewer would read the green channel as "green paint" when it means "roughness". A lit
    //    composite of all six is the honest single image, and that needs a shading pass; it is on the
    //    backlog rather than faked here.
    BuildLayerPreview(Layer)
    {
        const Host = document.createElement("div");
        Host.className = "layer-preview";

        const Hero = document.createElement("div");
        Hero.className = "lp-hero";
        // The checker shows through wherever the layer has NO coverage. A layer is a sparse contribution to
        // the stack, so "transparent here" is real information — a flat tile would imply full coverage.
        Hero.innerHTML = `<span class="lp-chk"></span><span class="lp-img"></span>`;

        const Note = document.createElement("div");
        Note.className = "lp-note";

        const Image = Hero.querySelector(".lp-img");
        const Generation = this.PreviewGeneration ?? 0;

        this.Capture(Layer.Token, "baseColour").then((Preview) => {
            if ((this.PreviewGeneration ?? 0) !== Generation) { return; }
            if (!Image.isConnected) { return; }

            if (!Preview)
            {
                Host.classList.add("lp-empty");
                Note.textContent = Layer.Paintable
                    ? "Nothing painted yet — paint a stroke to see it here."
                    : "No colour content on this layer yet.";
                return;
            }

            Image.style.backgroundImage = `url(${Preview.Image})`;
            Note.textContent = `Colour · ${Preview.Extent}² from the ${Preview.Atlas} atlas`;
        }).catch(() => {});

        Host.appendChild(Hero);
        Host.appendChild(Note);

        // The remaining channels as small tiles, in a paged carousel. Each is its own readback and each is
        // LABELLED, because a bare greyscale square gives the viewer no way to tell roughness from height.
        //
        // 📝 Derived channels are skipped: normal is computed from height at shade time and has no storage,
        //    so there is no atlas to read back for it.
        const Channels = CHANNEL_PANELS.filter(
            (P) => P.Key !== "baseColour" && P.Kind !== "derived");

        if (Channels.length === 0) { return Host; }

        const Strip = document.createElement("div");
        Strip.className = "lp-strip";

        const Rail = document.createElement("div");
        Rail.className = "lp-rail";

        const Prev = document.createElement("button");
        Prev.className = "lp-arrow prev";
        Prev.type = "button";
        Prev.title = "Previous channels";
        Prev.innerHTML = Icon("chevronLeft", 14);

        const Next = document.createElement("button");
        Next.className = "lp-arrow next";
        Next.type = "button";
        Next.title = "More channels";
        Next.innerHTML = Icon("chevron", 14);

        const Dots = document.createElement("div");
        Dots.className = "lp-dots";

        for (const Panel of Channels)
        {
            const Cell = document.createElement("div");
            Cell.className = "lp-cell";
            Cell.innerHTML = `<span class="lpc-tile"></span><span class="lpc-lb"></span>`;
            Cell.querySelector(".lpc-lb").textContent = Panel.Label;
            Cell.title = Panel.Label;

            const Tile = Cell.querySelector(".lpc-tile");
            this.Capture(Layer.Token, Panel.Key).then((Preview) => {
                if ((this.PreviewGeneration ?? 0) !== Generation) { return; }
                if (!Tile.isConnected) { return; }
                if (!Preview) { Cell.classList.add("lpc-empty"); return; }
                Tile.style.backgroundImage = `url(${Preview.Image})`;
            }).catch(() => {});

            Rail.appendChild(Cell);
        }

        // 🔴 Paging is measured off the rail's ACTUAL scroll extent, not computed from a hard-coded tile
        //    width times a count. The tiles are flex-sized against the card, so a fixed stride drifts out of
        //    step the moment the panel scale changes — and the last page would either stop short of the end
        //    or scroll past it, both of which strand a channel the user can never bring into view.
        const PageCount = () => Math.max(1, Math.ceil(Rail.scrollWidth / Math.max(1, Rail.clientWidth)));

        let Page = 0;

        const Sync = () => {
            const Pages   = PageCount();
            const Maximum = Math.max(0, Rail.scrollWidth - Rail.clientWidth);
            Page = Math.min(Page, Pages - 1);

            Rail.scrollTo({ left: Math.min(Page * Rail.clientWidth, Maximum), behavior: "smooth" });

            // Both arrows and the dots hide outright when everything already fits — a control that cannot
            // do anything is worse than no control, because it invites a press that appears to fail.
            const Paged = Pages > 1;
            Strip.classList.toggle("lp-paged", Paged);
            Prev.disabled = !Paged || Page === 0;
            Next.disabled = !Paged || Page >= Pages - 1;

            Dots.innerHTML = "";
            if (Paged)
            {
                for (let Index = 0; Index < Pages; Index += 1)
                {
                    const Dot = document.createElement("span");
                    Dot.className = "lp-dot" + (Index === Page ? " on" : "");
                    Dots.appendChild(Dot);
                }
            }
        };

        Prev.onclick = (Event) => { Event.stopPropagation(); Page -= 1; Sync(); };
        Next.onclick = (Event) => { Event.stopPropagation(); Page += 1; Sync(); };

        Strip.appendChild(Prev);
        Strip.appendChild(Rail);
        Strip.appendChild(Next);
        Host.appendChild(Strip);
        Host.appendChild(Dots);

        // 📝 Measured after layout. Called synchronously the element is still unattached, so scrollWidth and
        //    clientWidth are both 0 and every channel would look like it fits on one page.
        requestAnimationFrame(Sync);

        // 🔴 Re-measured whenever the rail's own width changes, because paging is a function of BOTH the tile
        //    count and the space available — and Sync() previously only ever ran from an arrow press. That is
        //    unreachable in exactly the state that needs it: when the rail shrinks with the arrows still
        //    hidden, there is no arrow to press, so the strip would keep claiming everything fits while
        //    channels sat out of view. Harmless with today's four tiles in a fixed-size card; a real defect
        //    the moment the channel set grows toward the ~14 planned, or the card is ever made resizable.
        if (typeof ResizeObserver === "function")
        {
            const Watch = new ResizeObserver(() => {
                // Disconnect once detached, or the observer outlives every pane rebuild and leaks one
                // callback per Refresh() for the lifetime of the session.
                if (!Rail.isConnected) { Watch.disconnect(); return; }
                Sync();
            });
            Watch.observe(Rail);
        }

        return Host;
    }

    // Paint one element's background from a layer's REAL atlas content.
    //
    // 🔴 This is what replaced SwatchOf() in the row thumbnail, the hero and the identity chip. Those three
    //    drew Layer.Values.baseColour — the AUTHORED value, one flat colour — which is a different thing
    //    from what the layer actually holds. A layer covered in ten strokes of ten colours still showed one
    //    solid square, so the preview agreed with the model while disagreeing with the paint.
    //
    // 📝 Fire-and-forget on purpose. Every caller builds its markup synchronously (Refresh() is sync and
    //    LayerCommand calls it without awaiting), so the element is returned immediately and the image lands
    //    when the readback resolves. `Fallback` is what shows until then, and stays if nothing was painted.
    FillFromAtlas(Element, Layer, Key, Fallback)
    {
        // The same generation guard BuildChannelPreview uses: the pane is rebuilt on every Refresh(), so a
        // slow readback must not paint itself into the element that replaced its own.
        const Generation = this.PreviewGeneration ?? 0;

        this.Capture(Layer.Token, Key ?? "baseColour").then((Preview) => {
            if ((this.PreviewGeneration ?? 0) !== Generation) { return; }
            if (!Element.isConnected) { return; }
            if (!Preview) { return; }

            Element.style.backgroundImage    = `url(${Preview.Image})`;
            // 🔴 Atlas content is pixel art at thumbnail size, so it is scaled with `pixelated` rather than
            //    smoothed. Bilinear scaling of a sparse painted atlas blurs isolated strokes into the
            //    transparent gutters and the thumbnail reads as empty.
            Element.style.backgroundSize     = "cover";
            Element.style.imageRendering     = "pixelated";
            Element.classList.add("from-atlas");
        }).catch(() => {});

        if (Fallback) { Element.style.background = Fallback; }
        return Element;
    }

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
            // 📝 Value mode is named as such rather than credited to the atlas. The tile is a flat authored
            //    value at that point, so "from the Material atlas" would be describing storage the tile is
            //    pointedly NOT showing — and the reassurance that the paint survives is the useful half.
            Note.textContent = ((Layer.Modes?.[Panel.Key] ?? "Value") === "Value")
                ? `Authored value. Any painted content is kept and returns on Texture.`
                : `${Preview.Extent}² from the ${Preview.Atlas} atlas`;
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
        this.CloseAddMenu();
        this.CloseMaskMenu();
        if (!this.OpenList) { return; }
        this.OpenList.classList.remove("open");
        this.OpenList = null;
    }

    //--------------------------------------------------------------------------------------------------
    //                                        ADD MENU
    //--------------------------------------------------------------------------------------------------

    CloseAddMenu()
    {
        this.Part.AddBar.classList.remove("open");
        this.AddOpen = false;
    }

    // The typed New Layer menu, ported from the reference's renderAddBar(): a header, one option per layer
    // type carrying its tinted glyph tile / name / description, and a footer stating where the layer lands.
    //
    // 📝 Rebuilt on each open so the capacity guard reflects the live layer count.
    RenderAddMenu()
    {
        const Menu = this.Part.AddMenu;
        Menu.innerHTML = "";

        const Room = this.Stack.Count < LayerCapacity;

        const Head = document.createElement("div");
        Head.className = "la-head";
        Head.textContent = "New Layer";
        Menu.appendChild(Head);

        for (const Type of LAYER_TYPES)
        {
            const Option = document.createElement("div");
            Option.className = "la-opt" + (Room ? "" : " disabled");
            Option.setAttribute("role", "menuitem");
            Option.dataset.type = Type.Label;
            // The reference tints the glyph tile with the type's own hue at 22 alpha over its full-strength ink.
            Option.innerHTML =
                `<span class="la-ico" style="background:${Type.Tag}22;color:${Type.Tag}">` +
                  `${Icon(Type.Glyph, 15)}</span>` +
                `<span class="la-tx"><span class="la-nm"></span><span class="la-ds"></span></span>`;
            Option.querySelector(".la-nm").textContent = Type.Label;
            Option.querySelector(".la-ds").textContent = Type.Note;

            if (Room)
            {
                Option.onclick = (Event) => {
                    Event.stopPropagation();
                    this.CloseAddMenu();
                    // 📝 "New Paint" / "New Fill" / … is the reference's own naming for a fresh layer.
                    this.Apply("add", {
                        Name:      `New ${Type.Label}`,
                        Kind:      Type.Kind,
                        Tag:       Type.Tag,
                        Preset:    Type.Preset    ?? null,
                        Generator: Type.Generator ?? null
                    });
                };
            }
            else
            {
                Option.title = `Layer cap of ${LayerCapacity} reached.`;
            }

            Menu.appendChild(Option);
        }

        const Foot = document.createElement("div");
        Foot.className = "la-foot";
        Foot.innerHTML = `${Icon("layers", 12)}<span>Adds above the selected layer</span>`;
        Menu.appendChild(Foot);
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

// The reference's switchRow: label pushed left, switch pushed right, on one line.
//
// 📝 Not PropertyRow. PropertyRow is an 88px label column on a grid, which is right for a properties pane
//    where a dozen rows have to line up; the expand has one switch and the reference puts it end-to-end.
function SwitchRow(Label, On, OnFlip)
{
    const Row = document.createElement("div");
    Row.className = "switch-row";
    const Tag = document.createElement("span");
    Tag.className = "sr-label";
    Tag.textContent = Label;
    Row.appendChild(Tag);
    Row.appendChild(BuildSwitch(On, OnFlip));
    return Row;
}

// The reference's ddCtl/sliderCtl shape: caption ABOVE its field rather than beside it.
function Control(Label, Field)
{
    const Host = document.createElement("div");
    Host.className = "ctl";
    const Tag = document.createElement("div");
    Tag.className = "ctl-label";
    Tag.textContent = Label;
    Host.appendChild(Tag);
    Host.appendChild(Field);
    return Host;
}

const Rule = () => {
    const Line = document.createElement("div");
    Line.className = "lse-sep";
    return Line;
};

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
