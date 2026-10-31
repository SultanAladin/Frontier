// 📝 Retained evidence capture for the Editor Workspace prototype.
// Produces the JPEGs stored under `evidence/` from a real headless Chromium session, so the
// screenshots in the delivery can always be regenerated from the current sources.
//
// Usage:
//   node tools/CaptureWorkspaceScreens.mjs [baseUrl]
//
// Requirements:
//   * puppeteer-core installed (`npm install --no-save puppeteer-core`)
//   * a Chromium/Chrome build whose executable is passed through `CHROMIUM_EXECUTABLE`
//     (defaults to the @sparticuz/chromium binary when that package is present)
//   * the prototype served over HTTP (ES modules and woff2 fonts do not load from file://),
//     e.g. `python3 -m http.server 8130` from the repository root.
//
// The script performs real interactions (record selection, record creation, inspector tab
// switch) instead of mutating state from the console, so the captures reflect user-visible
// behaviour.

import { mkdir } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const ProjectRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const EvidenceRoot = resolve(ProjectRoot, "evidence");
const BaseUrl = process.argv[2] || "http://127.0.0.1:8130/";

const { default: puppeteer } = await import("puppeteer-core");

async function ResolveExecutable()
{
    if (process.env.CHROMIUM_EXECUTABLE)
    {
        return process.env.CHROMIUM_EXECUTABLE;
    }

    const { default: chromium } = await import("@sparticuz/chromium");
    return chromium.executablePath();
}

const LaunchArguments = [
    "--no-sandbox",
    "--disable-setuid-sandbox",
    "--enable-unsafe-swiftshader",
    "--use-gl=angle",
    "--use-angle=swiftshader",
    "--hide-scrollbars"
];

const Settle = (Milliseconds) => new Promise((Resolution) => setTimeout(Resolution, Milliseconds));

await mkdir(EvidenceRoot, { recursive: true });

const Browser = await puppeteer.launch({
    args: LaunchArguments,
    executablePath: await ResolveExecutable(),
    headless: true
});

const Page = await Browser.newPage();
const PageErrors = [];

Page.on("pageerror", (Error) => PageErrors.push(Error.message));
Page.on("console", (Message) =>
{
    if (Message.type() === "error")
    {
        PageErrors.push(Message.text());
    }
});

await Page.setViewport({ width: 1600, height: 1000, deviceScaleFactor: 1 });
await Page.goto(BaseUrl, { waitUntil: "networkidle0" });
await Settle(2400);

async function Capture(Name, Clip)
{
    await Page.screenshot({
        path: resolve(EvidenceRoot, `${Name}.jpg`),
        type: "jpeg",
        quality: 88,
        ...(Clip ? { clip: Clip } : {})
    });
    process.stdout.write(`captured ${Name}.jpg\n`);
}

async function PanelClip(Identifier)
{
    return Page.$eval(Identifier, (Node) =>
    {
        const Rect = Node.getBoundingClientRect();
        return { x: Math.round(Rect.left), y: Math.round(Rect.top), width: Math.round(Rect.width), height: Math.round(Rect.height) };
    });
}

// 1. Full workspace: docked outliner, live viewport, inspector.
await Capture("WorkspaceOverview");

// 2. Viewport close-up with the selection annotation and scan timeline.
await Capture("ViewportSelection", await PanelClip("#ViewportPanel"));

// 3. Inspector after switching to the surface tab (sliders drive the live shader).
await Page.click('.panel-tab[data-inspector-tab="surface"]');
await Settle(700);
await Capture("InspectorSurface", await PanelClip("#InspectorPanel"));

// 📝 Preset pills must drive the live surface, not just look active.
await Page.click('.preset-pill[data-surface-preset="1"]');
await Settle(700);
const PresetProbe = await Page.evaluate(() => ({
    active: document.querySelectorAll(".preset-pill.is-active").length,
    activeLabel: document.querySelector(".preset-pill.is-active")?.textContent,
    chip: document.getElementById("SurfaceModeChip").textContent.trim(),
    sliders: [...document.querySelectorAll("[data-surface-property]")].map((Row) => Row.querySelector(".slider-value").textContent)
}));
await Capture("InspectorPreset", await PanelClip("#InspectorPanel"));
await Page.click('.preset-pill[data-surface-preset="0"]');
await Settle(600);

// 4. Record creation: add a sphere and frame it.
await Page.click('.panel-tab[data-inspector-tab="object"]');
await Page.click("#AddRecordButton");
await Page.click('#PrimitiveMenu [data-primitive="sphere"]');
await Settle(1400);
await Capture("RecordAdded", { x: 334, y: 72, width: 896, height: 888 });

// 5. Narrow dock: proves the three panels stay docked without overlap at 1280x800.
await Page.setViewport({ width: 1280, height: 800, deviceScaleFactor: 1 });
await Page.goto(BaseUrl, { waitUntil: "networkidle0" });
await Settle(2000);
await Capture("NarrowDock");

const LayoutReport = await Page.evaluate(() =>
{
    const Bounds = (Identifier) =>
    {
        const Rect = document.getElementById(Identifier).getBoundingClientRect();
        return { left: Math.round(Rect.left), right: Math.round(Rect.right), width: Math.round(Rect.width) };
    };

    const Outliner = Bounds("OutlinerPanel");
    const Viewport = Bounds("ViewportPanel");
    const Inspector = Bounds("InspectorPanel");

    return {
        Outliner,
        Viewport,
        Inspector,
        Overlap: Viewport.left < Outliner.right || Inspector.left < Viewport.right,
        HorizontalScroll: document.documentElement.scrollWidth > document.documentElement.clientWidth
    };
});

process.stdout.write(`${JSON.stringify({ LayoutReport, PresetProbe, PageErrors }, null, 2)}\n`);

await Browser.close();
