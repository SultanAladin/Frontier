import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { LobbyScreen } from "../Source/LobbyScreen.js";
import { OverlayDock } from "../Source/OverlayDock.js";
import { LevelCatalog, ResolveLevelKey, QueryLevel } from "../Source/LevelCatalog.js";

const Root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

// 📝 Minimal DOM: the lobby touches ids, class lists, styles and a click listener, nothing else.
class FakeElement
{
    constructor(Id = "")
    {
        this.id = Id;
        this.style = {};
        this.dataset = {};
        this.children = [];
        this.Listeners = {};
        this.textContent = "";
        this.disabled = false;
        this.Attributes = {};
        this.Members = new globalThis.Set();
        this.classList = {
            add: (...Names) => Names.forEach((Name) => this.Members.add(Name)),
            remove: (...Names) => Names.forEach((Name) => this.Members.delete(Name)),
            clear: () => this.Members.clear(),
            toggle: (Name, Force) => { const On = Force === undefined ? !this.Members.has(Name) : Force; if (On) this.Members.add(Name); else this.Members.delete(Name); return On; },
            contains: (Name) => this.Members.has(Name)
        };
    }
    addEventListener(Name, Handler) { (this.Listeners[Name] = this.Listeners[Name] || []).push(Handler); }
    get className() { return [...this.Members].join(" "); }
    set className(Value) { this.classList.clear(); this.classList.add(...String(Value).split(/\s+/).filter(Boolean)); }
    append(...Children) { Children.forEach((Child) => this.appendChild(Child)); }
    appendChild(Child) { this.children.push(Child); return Child; }
    dispatch(Name, Event = {}) { (this.Listeners[Name] || []).forEach((Handler) => Handler({ preventDefault() {}, target: this, ...Event })); }
    setAttribute(Name, Value) { this.Attributes[Name] = Value; }
    querySelector() { return new FakeElement(); }
    focus() { this.Focused = true; }
    blur() { this.Focused = false; }
}

let Elements = new Map();
let WindowListeners = {};

function AssignDom(Search = "")
{
    Elements = new Map();
    WindowListeners = {};
    // 📝 index.html ships the entry button disabled; SetReady is the only thing that may enable it.
    Elements.set("lobby-enter", Object.assign(new FakeElement("lobby-enter"), { disabled: true }));
    globalThis.HTMLElement = FakeElement;
    globalThis.HTMLInputElement = class extends FakeElement {};
    globalThis.HTMLSelectElement = class extends FakeElement {};
    globalThis.document = {
        getElementById(Id) { if (!Elements.has(Id)) Elements.set(Id, new FakeElement(Id)); return Elements.get(Id); },
        createElement(Tag) { return new FakeElement(Tag); },
        querySelectorAll() { return []; },
        body: new FakeElement("body"),
        activeElement: null
    };
    globalThis.window = globalThis;
    globalThis.location = { search: Search };
    globalThis.addEventListener = (Name, Handler) => { (WindowListeners[Name] = WindowListeners[Name] || []).push(Handler); };
}

const PressKey = (Event) => (WindowListeners.keydown || []).forEach((Handler) => Handler({ preventDefault() {}, repeat: false, ...Event }));

function ConstructHost()
{
    return { Keys: new Set(["KeyW"]), Handovers: 0, OnLobbyEntered() { this.Handovers += 1; } };
}

test("the build ships the lobby and one level, with no other level content in Source", () => {
    const Sources = fs.readdirSync(path.join(Root, "Source"));
    for (const Banned of ["DDayBattleSequence.js", "DDayStructure.js", "CityPursuitStructure.js", "PolicePursuitSequence.js",
                          "InversionArenaStructure.js", "RoadStudioAdapter.js", "CollapseSequence.js", "TramSequence.js"])
        assert.ok(!Sources.includes(Banned), `${Banned} belongs to a level this build removed`);

    const Text = (File) => fs.readFileSync(path.join(Root, File), "utf8");
    for (const File of Sources.filter((Name) => Name.endsWith(".js")).map((Name) => path.join("Source", Name)))
    {
        const Source = Text(File);
        assert.doesNotMatch(Source, /DDayStructure|InversionArenaStructure|CityPursuitStructure|RoadStudio/, File);
    }

    // 📝 One card, one entry point, no workspace tabs from the multi-level parent.
    const Index = Text("index.html");
    assert.match(Index, /id="lobby"/);
    assert.equal((Index.match(/class="lobby-enter"/g) || []).length, 1);
    assert.doesNotMatch(Index, /data-workspace-tab|open-dday|open-city-pursuit|workspace-tabs/);
    assert.match(Index, /Style\/Lobby\.css/);
    // 📝 The level name no longer hangs as a notch under the viewport tabs, and the columns close one at a time.
    assert.doesNotMatch(Index, /scene-pill/);
    assert.match(Index, /data-overlay="Outliner"/);
    assert.match(Index, /data-overlay="Inspector"/);
    assert.doesNotMatch(Index, /data-overlay="Panel"/);
});

test("the lobby reports real bring-up progress and refuses entry until the scene is ready", () => {
    AssignDom();
    const Host = ConstructHost();
    const Lobby = new LobbyScreen(Host);
    const Enter = Elements.get("lobby-enter");

    assert.equal(Lobby.Visible, true);
    assert.equal(Enter.disabled, true, "entry stays disabled during bring-up");

    Lobby.SetProgress("Compiling the cabin, mirrors and sparks", 0.89);
    assert.equal(Elements.get("lobby-status").textContent, "Compiling the cabin, mirrors and sparks");
    assert.equal(Elements.get("lobby-progress-bar").style.width, "89%");

    PressKey({ code: "Enter" });
    assert.equal(Lobby.Visible, true, "Enter before ready must not start the level");

    Lobby.SetReady();
    assert.equal(Enter.disabled, false);
    assert.ok(Elements.get("lobby").classList.contains("ready"));
});

test("entering hands the keyboard to the car exactly once and Esc returns to the lobby", () => {
    AssignDom();
    const Host = ConstructHost();
    const Lobby = new LobbyScreen(Host);
    Lobby.SetReady();

    PressKey({ code: "Enter" });
    assert.equal(Lobby.Visible, false);
    assert.equal(Host.Handovers, 1);
    assert.equal(Host.Keys.size, 0, "driving keys held at the lobby must not carry into the level");
    assert.ok(Elements.get("lobby").classList.contains("leaving"));

    Lobby.Enter();
    assert.equal(Host.Handovers, 1, "a second hand-over is ignored");

    PressKey({ code: "Escape" });
    assert.equal(Lobby.Visible, true);
    assert.ok(!Elements.get("lobby").classList.contains("leaving"));
    assert.equal(Elements.get("lobby-enter").disabled, false);
});

test("?lobby=0 boots straight into the level for harnesses and captures", () => {
    AssignDom("?lobby=0");
    const Host = ConstructHost();
    const Lobby = new LobbyScreen(Host);

    assert.equal(Lobby.Visible, false);
    assert.ok(Elements.get("lobby").classList.contains("hidden"));
    Lobby.SetProgress("Ignored after entry", 0.4);
    assert.equal(Elements.get("lobby-status").textContent, "Ignored after entry");
});

test("a bring-up failure is reported on the card instead of a silent disabled button", () => {
    AssignDom();
    const Lobby = new LobbyScreen(ConstructHost());
    Lobby.SetFailure("WebGPU device rejected");

    assert.equal(Elements.get("lobby-status").textContent, "Bring-up failed · WebGPU device rejected");
    assert.ok(Elements.get("lobby").classList.contains("failed"));
    assert.equal(Elements.get("lobby-enter").disabled, true);
});

test("the lobby lists the catalogue, marks the current level and shows its copy", () => {
    AssignDom();
    const Lobby = new LobbyScreen(ConstructHost());
    const Rows = Elements.get("lobby-levels").children;

    assert.equal(Rows.length, LevelCatalog.length);
    assert.equal(Elements.get("lobby-level-count").textContent, String(LevelCatalog.length));
    assert.equal(Rows[0].dataset.Level, LevelCatalog[0].Key);
    assert.ok(Rows[0].classList.contains("current"));
    assert.equal(Rows[0].Attributes["aria-pressed"], "true");
    assert.equal(Elements.get("lobby-title").textContent, LevelCatalog[0].Title);
    assert.equal(Elements.get("lobby-chips").children.length, LevelCatalog[0].Contents.length);
    assert.equal(Lobby.LevelKey, LevelCatalog[0].Key);
});

test("level keys resolve through the catalogue and unknown keys fall back", () => {
    assert.ok(LevelCatalog.length >= 1);
    for (const Entry of LevelCatalog)
    {
        assert.ok(Entry.Key && Entry.Title, "every level needs a key and a title");
        assert.equal(QueryLevel(Entry.Key), Entry);
        assert.equal(ResolveLevelKey(Entry.Key), Entry.Key);
    }
    assert.equal(ResolveLevelKey("dday"), LevelCatalog[0].Key);
    assert.equal(ResolveLevelKey(undefined), LevelCatalog[0].Key);
});

test("each editor column closes on its own and P folds both", () => {
    AssignDom();
    const Settings = { HudVisible: false, InsetEnabled: false, OutlinerVisible: true, InspectorVisible: true };
    const Changes = [];
    const Dock = new OverlayDock(Settings, (Name) => Changes.push(Name));

    Dock.Toggle("Outliner");
    assert.equal(Settings.OutlinerVisible, false);
    assert.ok(document.body.classList.contains("outliner-hidden"));
    assert.ok(!document.body.classList.contains("inspector-hidden"), "closing the outliner must leave the inspector standing");
    assert.equal(Settings.InspectorVisible, true);

    Dock.Toggle("Inspector");
    assert.ok(document.body.classList.contains("inspector-hidden"));
    assert.ok(document.body.classList.contains("outliner-hidden"));

    Dock.Toggle("Outliner");
    assert.ok(!document.body.classList.contains("outliner-hidden"), "the outliner comes back alone");
    assert.ok(document.body.classList.contains("inspector-hidden"), "and the inspector stays closed");

    Dock.Toggle("Panels");
    assert.ok(!document.body.classList.contains("inspector-hidden") && !document.body.classList.contains("outliner-hidden"));
    Dock.Toggle("Panels");
    assert.ok(document.body.classList.contains("inspector-hidden") && document.body.classList.contains("outliner-hidden"));
    assert.deepEqual(Changes, ["Outliner", "Inspector", "Outliner", "Panels", "Panels"]);
});
