import { test } from "node:test";
import assert from "node:assert/strict";
import { DeviceExchange, IsSoftwareAdapter } from "../Source/DeviceExchange.js";

class ClassList
{
    constructor(...Names) { this.Names = new Set(Names); }
    add(Name) { this.Names.add(Name); }
    remove(Name) { this.Names.delete(Name); }
    contains(Name) { return this.Names.has(Name); }
}

test("missing WebGPU, runtime failures, and first-frame reveal are visible", async () =>
{
    const Previous = Object.fromEntries(["window", "location", "document", "navigator", "addEventListener", "IlluminationExhibit", "GPUMapMode", "ImageData"]
        .map((Name) => [Name, Object.getOwnPropertyDescriptor(globalThis, Name)]));
    const Caption = { textContent: "" }, Bar = { style: {} };
    const Overlay = { classList: new ClassList(), querySelector(Selector) { return Selector === ".bring-up-caption" ? Caption : Bar; } };
    const Failure = { classList: new ClassList("hidden"), children: [], appendChild(Line) { this.children.push(Line); } };
    const Veil = { style: { opacity: "1" } };
    const Canvas = { getContext() { return { putImageData(Image) { Canvas.LastImage = Image; } }; } };
    const Elements = { "bring-up": Overlay, failure: Failure, "cinematic-fade": Veil, capture: Canvas };
    const Listeners = {};
    const OriginalConsoleError = console.error;
    try
    {
        console.error = () => {}; // The deliberately unsupported adapter is reported by the host.
        globalThis.window = globalThis;
        globalThis.location = { pathname: "/Lobby.html", search: "" };
        globalThis.document = { getElementById(Id) { return Elements[Id] || null; }, createElement() { return { textContent: "" }; } };
        Object.defineProperty(globalThis, "navigator", { value: { gpu: undefined }, configurable: true });
        globalThis.addEventListener = (Name, Listener) => { Listeners[Name] = Listener; };
        await import("../Source/IlluminationHost.js");
        await new Promise((Resolve) => setImmediate(Resolve));
        const Host = globalThis.IlluminationExhibit;
        assert.ok(Host.Failed);
        assert.ok(Overlay.classList.contains("hidden"));
        assert.ok(!Failure.classList.contains("hidden"));
        assert.match(Failure.children[0].textContent, /WebGPU.*unavailable/);

        // Animation errors and rejected asynchronous work must not leave the loading screen hanging.
        for (const [Name, Event] of [["error", { message: "frame failed" }], ["unhandledrejection", { reason: new Error("queue failed") }]])
        {
            Host.Failed = false;
            Overlay.classList.remove("hidden");
            Failure.classList.add("hidden");
            Failure.children = [];
            Listeners[Name](Event);
            assert.ok(Host.Failed);
            assert.ok(Overlay.classList.contains("hidden"));
            assert.ok(!Failure.classList.contains("hidden"));
            assert.match(Failure.children[0].textContent, /failed/);
            Host.ReportFailure("duplicate", true);
            assert.equal(Failure.children.length, 1);
        }

        Host.Failed = false;
        Overlay.classList.remove("hidden");
        Host.RevealLobby();
        assert.ok(Host.LobbyPresented);
        assert.ok(Overlay.classList.contains("hidden"));
        assert.equal(Caption.textContent, "Ready");
        assert.equal(Veil.style.opacity, "0");

        // If a resize supersedes an in-flight readback, the old buffer cannot reveal the Lobby or clear the new pending map.
        globalThis.GPUMapMode = { READ: 1 };
        globalThis.ImageData = class { constructor(Data, Width, Height) { Object.assign(this, { Data, Width, Height }); } };
        const Readback = () => ({
            mapAsync() { return new Promise((Resolve) => { this.Resolve = Resolve; }); },
            getMappedRange() { return new ArrayBuffer(256); },
            unmap() { this.Unmapped = true; },
            destroy() { this.Destroyed = true; }
        });
        Host.ScreenExtent = [2, 1];
        Host.CaptureStride = 256;
        const Old = Readback(), Current = Readback();
        Host.CaptureRecords = Old;
        const OldResult = Host.ProjectCapture(0);
        Host.CaptureRecords = Current;
        Host.CapturePending = false;
        const CurrentResult = Host.ProjectCapture(1);
        Old.Resolve();
        assert.equal(await OldResult, false);
        assert.ok(Old.Unmapped && Old.Destroyed && Host.CapturePending);
        Current.Resolve();
        assert.equal(await CurrentResult, true);
        assert.equal(Host.CapturedFrame, 1);
        assert.equal(Host.CapturePending, false);
        assert.equal(Canvas.LastImage.Width, 2);
    }
    finally
    {
        console.error = OriginalConsoleError;
        for (const [Name, Descriptor] of Object.entries(Previous))
            if (Descriptor) Object.defineProperty(globalThis, Name, Descriptor); else delete globalThis[Name];
    }
});

test("WebGPU negotiation explains an unavailable adapter", async () =>
{
    const Previous = Object.getOwnPropertyDescriptor(globalThis, "navigator");
    Object.defineProperty(globalThis, "navigator", { value: { gpu: { async requestAdapter() { return null; } } }, configurable: true });
    try
    {
        await assert.rejects(new DeviceExchange().ConstructDevice(), /No WebGPU adapter/);
    }
    finally
    {
        if (Previous) Object.defineProperty(globalThis, "navigator", Previous); else delete globalThis.navigator;
    }
});

test("software adapter detection does not force readback on hardware", () =>
{
    assert.equal(IsSoftwareAdapter({ info: { vendor: "google", architecture: "swiftshader" } }), true);
    assert.equal(IsSoftwareAdapter({ info: { description: "Mesa llvmpipe" } }), true);
    assert.equal(IsSoftwareAdapter({ isFallbackAdapter: true }), true);
    assert.equal(IsSoftwareAdapter({ info: { vendor: "intel", architecture: "xe" } }), false);
    assert.equal(IsSoftwareAdapter({ info: {} }), false);
    assert.equal(IsSoftwareAdapter(null), false);
});
