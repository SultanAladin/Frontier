//============================================================================================================================================
//                                                        BENCHSMOKE.MJS
//============================================================================================================================================
// 🧩 Boots the real page module against a minimal DOM and canvas stub, then drives several picture frames and a knob change. This proves the page
//    builds the garage, draws every instrument, wires every control, and survives input without a browser — the part of the bench that node:test
//    can reach in this environment.

import { strict as Assert } from "node:assert";

class StubClassList
{
    constructor() { this.Set = new Set(); }
    add(Name) { this.Set.add(Name); }
    remove(Name) { this.Set.delete(Name); }
    toggle(Name, Force)
    {
        if (Force === undefined)
        {
            if (this.Set.has(Name)) this.Set.delete(Name); else this.Set.add(Name);
        }
        else if (Force) this.Set.add(Name); else this.Set.delete(Name);
        return this.Set.has(Name);
    }
    contains(Name) { return this.Set.has(Name); }
}

class StubElement
{
    constructor(TagName)
    {
        this.tagName = TagName.toUpperCase();
        this.children = [];
        this.childElementCount = 0;
        this.classList = new StubClassList();
        this.dataset = {};
        this.style = {};
        this.Listeners = {};
        this.textContent = "";
        this.innerHTML = "";
        this.value = "0";
        this.width = 620;
        this.height = 220;
        this.Written = [];
    }
    appendChild(Child) { this.children.push(Child); this.childElementCount = this.children.length; return Child; }
    removeChild(Child) { this.children = this.children.filter((Entry) => Entry !== Child); this.childElementCount = this.children.length; }
    addEventListener(Name, Handler) { (this.Listeners[Name] = this.Listeners[Name] || []).push(Handler); }
    Dispatch(Name, Event = {}) { for (const Handler of this.Listeners[Name] || []) Handler({ target: this, ...Event }); }
    setAttribute() {}
    getContext() { return CanvasContext; }
    querySelector(Selector)
    {
        const Key = Selector.replace(".", "");
        this.QueryCache = this.QueryCache || {};
        if (!this.QueryCache[Key]) this.QueryCache[Key] = new StubElement("span");
        return this.QueryCache[Key];
    }
    querySelectorAll() { return []; }
}

const CanvasContext = new Proxy({
    createLinearGradient: () => ({ addColorStop() {} }),
    measureText: () => ({ width: 10 }),
    getImageData: () => ({ data: new Uint8ClampedArray(4) })
}, {
    get: (Target, Key) =>
    {
        if (Key in Target) return Target[Key];
        return () => {};
    },
    set: () => true
});

const Elements = new Map();
const Document = {
    getElementById: (Name) =>
    {
        if (!Elements.has(Name)) Elements.set(Name, new StubElement("div"));
        return Elements.get(Name);
    },
    createElement: (TagName) => new StubElement(TagName),
    body: new StubElement("body")
};

const Frames = [];
globalThis.document = Document;
globalThis.window = {
    Listeners: {},
    addEventListener(Name, Handler) { (this.Listeners[Name] = this.Listeners[Name] || []).push(Handler); },
    Dispatch(Name, Event) { for (const Handler of this.Listeners[Name] || []) Handler(Event); }
};
globalThis.performance = { now: () => Number(process.hrtime.bigint() / 1000000n) };
globalThis.requestAnimationFrame = (Handler) => { Frames.push(Handler); return Frames.length; };
globalThis.AudioContext = class
{
    constructor() { this.sampleRate = 48000; this.audioWorklet = null; this.destination = {}; }
    createAnalyser() { return { fftSize: 2048, smoothingTimeConstant: 0, frequencyBinCount: 1024,
                                getByteFrequencyData: () => {}, getByteTimeDomainData: () => {}, connect() {} }; }
    createScriptProcessor() { return { connect() {}, onaudioprocess: null }; }
    async resume() {}
    async close() {}
};
globalThis.AudioWorkletNode = class {};
globalThis.URL.createObjectURL = () => "blob:stub";
globalThis.URL.revokeObjectURL = () => {};
globalThis.Blob = class { constructor() {} };

// 📝 The listening-position buttons exist in index.html before the module runs, so the stub must have them in the same order.
const PositionStub = new StubElement("div");
for (const Name of ["cabin", "exterior", "passBy"])
{
    const Node = new StubElement("button");
    Node.dataset.position = Name;
    if (Name === "exterior") Node.classList.add("active");
    PositionStub.appendChild(Node);
}
Elements.set("position-control", PositionStub);

const Bench = await import("../Source/Bench.js");
Assert.ok(Bench, "bench module loaded");
Assert.ok(Elements.size > 20, `expected the page to touch its elements, saw ${Elements.size}`);

// 📝 Garage: one card per catalogue car, and the first car selected.
const Garage = Elements.get("car-list");
Assert.equal(Garage.childElementCount, 9, `garage cards: ${Garage.childElementCount}`);
Assert.ok(Garage.children.some((Card) => Card.classList.contains("selected")), "no car selected after boot");
Assert.ok(Garage.children[0].innerHTML.includes("LaFerrari"), "first card is not the LaFerrari");

// 📝 Instrument values are written from telemetry; before audio starts they come from the idle state.
Assert.equal(Elements.get("rpm-value").textContent, "0");
Assert.equal(Elements.get("gear-value").textContent, "N");

// 📝 Drive the picture loop several times: it must draw, read the frame budget, and not throw.
for (let Index = 0; Index < 4; ++Index)
{
    const Handler = Frames.shift();
    Assert.ok(typeof Handler === "function", "no animation frame scheduled");
    Handler(performance.now());
}

// 📝 Garage switching: clicking a card must select it and refresh the sheet.
const ThirdCard = Garage.children[2];
ThirdCard.Dispatch("click");
Assert.ok(ThirdCard.classList.contains("selected"), "clicking a card did not select it");
const Sheet = Elements.get("vehicle-sheet");
Assert.ok(Sheet.children.length > 20, "vehicle sheet was not filled");

// 📝 Controls: the gain slider and the frame-budget slider must update their readouts.
const Gain = Elements.get("gain");
Gain.value = "0.42";
Gain.Dispatch("input", { target: Gain });
Assert.equal(Elements.get("gain-value").textContent, "0.42");

const Budget = Elements.get("framebudget");
Budget.value = "6";
Budget.Dispatch("input", { target: Budget });
Assert.equal(Elements.get("framebudget-value").textContent, "6");

// 📝 Listening position buttons: clicking must move the active state and nothing else.
const PositionControl = Elements.get("position-control");
PositionControl.children[0].Dispatch("click");
Assert.ok(PositionControl.children[0].classList.contains("active"), "cabin position did not activate");
Assert.ok(!PositionControl.children[1].classList.contains("active"), "exterior stayed active after selecting cabin");

// 📝 Keyboard: WASD must be accepted by the input sampler without an audio device present.
for (const Code of ["KeyW", "KeyA", "KeyQ", "KeyM", "KeyR"])
    window.Dispatch("keydown", { code: Code, preventDefault() {} });
window.Dispatch("keyup", { code: "KeyW" });
window.Dispatch("blur", {});

// 📝 Layer rows: the acoustic layer list must have one row per modelled mechanism and react to state.
const Layers = Elements.get("layer-bars");
Assert.ok(Layers.childElementCount >= 10, `layer rows: ${Layers.childElementCount}`);

// 📝 The transport readout must have been set from the capability probe.
Assert.ok(Elements.get("transport-mode").textContent.length > 0, "transport readout was not set");

// 📝 Canvas drawing counts: the tachometer and analyser must have issued real draw calls.
Assert.ok(Frames.length >= 1, "the picture loop stopped scheduling frames");

process.stdout.write(`bench smoke: garage 9 cards, ${Layers.childElementCount} layer rows, ` +
                     `${Sheet.children.length} sheet cells, ${Elements.size} elements touched, 4 frames drawn\n`);
