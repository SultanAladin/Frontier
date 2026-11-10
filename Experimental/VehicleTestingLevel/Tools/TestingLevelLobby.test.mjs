import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { LobbyScreen } from "../Source/LobbyScreen.js";

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
        const Members = new globalThis.Set();
        this.classList = {
            add: (...Names) => Names.forEach((Name) => Members.add(Name)),
            remove: (...Names) => Names.forEach((Name) => Members.delete(Name)),
            toggle: (Name, Force) => { const On = Force === undefined ? !Members.has(Name) : Force; if (On) Members.add(Name); else Members.delete(Name); return On; },
            contains: (Name) => Members.has(Name)
        };
    }
    addEventListener(Name, Handler) { (this.Listeners[Name] = this.Listeners[Name] || []).push(Handler); }
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
        assert.doesNotMatch(Source, /LevelCatalog|DDayStructure|InversionArenaStructure|CityPursuitStructure|RoadStudio/, File);
    }

    // 📝 One card, one entry point, no workspace tabs from the multi-level parent.
    const Index = Text("index.html");
    assert.match(Index, /id="lobby"/);
    assert.equal((Index.match(/class="lobby-enter"/g) || []).length, 1);
    assert.doesNotMatch(Index, /data-workspace-tab|open-dday|open-city-pursuit|workspace-tabs/);
    assert.match(Index, /Style\/Lobby\.css/);
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
