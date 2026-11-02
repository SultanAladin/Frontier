/*====================================================================================================================================
                                                        LAUNCHERVALIDATION.MJS
====================================================================================================================================*/
// 🧩 Rendered Chromium validation and retained captures for the launcher prototype

import { chromium as PlaywrightChromium } from "playwright-core";
import ServerlessChromium from "@sparticuz/chromium";
import { mkdir, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

const PrototypeDirectory = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const EvidenceDirectory = path.resolve(PrototypeDirectory, "../../Exhibits/LauncherStorefront");
const ImageDirectory = path.join(EvidenceDirectory, "Images");
const ResultDirectory = path.join(EvidenceDirectory, "Results");
await mkdir(ImageDirectory, { recursive: true });
await mkdir(ResultDirectory, { recursive: true });

const ConsoleErrors = [];
const PageErrors = [];
const Checks = [];
function Confirm(Condition, Label)
{
    Checks.push({ Label, Passed: Boolean(Condition) });
    if (!Condition) throw new Error(`Validation failed: ${Label}`);
}

const Browser = await PlaywrightChromium.launch({
    executablePath: await ServerlessChromium.executablePath(),
    args: ServerlessChromium.args,
    headless: true
});
const Context = await Browser.newContext({ viewport: { width: 1440, height: 960 }, deviceScaleFactor: 1 });
const Page = await Context.newPage();
Page.on("console", Message => { if (Message.type() === "error") ConsoleErrors.push(Message.text()); });
Page.on("pageerror", Error => PageErrors.push(Error.message));
const DocumentResponse = await Page.goto("http://127.0.0.1:5173", { waitUntil: "networkidle" });
await Page.evaluate(() => document.fonts.ready);

Confirm(await Page.title() === "Frontier — Games", "Launcher document loads");
Confirm(DocumentResponse.headers()["cache-control"]?.includes("no-store"), "Preview document disables stale caching");
const CssResponse = await fetch("http://127.0.0.1:5173/styles.css?v=20261102-3", { method: "HEAD" });
const ScriptResponse = await fetch("http://127.0.0.1:5173/launcher.js?v=20261102-3", { method: "HEAD" });
Confirm(CssResponse.headers.get("cache-control")?.includes("no-store"), "Stylesheet disables stale caching");
Confirm(ScriptResponse.headers.get("cache-control")?.includes("no-store"), "Launcher script disables stale caching");
Confirm(await Page.locator('link[rel="stylesheet"]').getAttribute("href") === "styles.css?v=20261102-3", "Stylesheet uses a cache-busting revision");
Confirm(await Page.locator('script[type="module"]').getAttribute("src") === "launcher.js?v=20261102-3", "Launcher script uses a cache-busting revision");
Confirm(await Page.evaluate(() => getComputedStyle(document.documentElement).getPropertyValue("--accent").trim()) === "#66b2ff", "Blue accent replaces the pale lime accent");
Confirm(await Page.locator(".sidebar.dock").isVisible(), "Floating dock sidebar renders");
Confirm(await Page.locator("#ViewStore").isVisible(), "Store is the default destination");
Confirm(await Page.locator("#StoreGrid .game-card").count() === 5, "Discover shows one uncluttered row of five games");
Confirm(await Page.locator("#HeroTitle").textContent() === "Control", "Featured artwork and title render");
Confirm(await Page.locator("#SidebarTransfer").isVisible(), "Persistent download card remains available");
Confirm(await Page.locator("#StoreGrid .card-icon-row").count() === 5, "Store cards expose grid icon actions");
await Page.locator("#StoreDensityToggle").click();
Confirm(await Page.locator("#StoreGrid").evaluate(Element => Element.classList.contains("compact")), "Store compact grid toggle works");
await Page.locator("#StoreDensityToggle").click();
await Page.waitForTimeout(400);
await Page.screenshot({ path: path.join(ImageDirectory, "Store.png"), fullPage: true });

await Page.locator("#HeroNext").click();
Confirm(await Page.locator("#HeroTitle").textContent() === "Cyberpunk 2077", "Featured carousel advances");
await Page.locator("#HeroPrevious").click();
await Page.locator("#HeroWishlist").click();
Confirm(await Page.locator("#HeroWishlist").getAttribute("aria-pressed") === "true", "Wishlist toggle persists on featured game");
await Page.locator('[data-store-tab="wishlist"]').click();
Confirm(await Page.locator("#StoreGrid .game-card").count() === 1, "Wishlist tab filters the catalogue");
await Page.locator('[data-store-tab="discover"]').click();
await Page.locator('[data-category="racing"]').click();
Confirm(await Page.locator("#StoreGrid .game-card").count() === 1, "Genre pill filters the store");
await Page.locator('[data-category="all"]').click();
await Page.locator("#Search").fill("elden");
Confirm(await Page.locator("#StoreGrid .game-card").count() === 1, "Search filters visible games");
await Page.locator("#Search").fill("");

await Page.locator('.nav-link[data-view="library"]').click();
Confirm(await Page.locator("#ViewLibrary").isVisible(), "Library navigation works");
Confirm(await Page.locator("#LibraryGrid .game-card").count() === 4, "Owned games appear in the library");
await Page.waitForTimeout(400);
await Page.evaluate(() => { document.getElementById("Toast").hidden = true; });
await Page.screenshot({ path: path.join(ImageDirectory, "Library.png"), fullPage: true });
await Page.locator('[data-library-filter="updates"]').click();
Confirm(await Page.locator("#LibraryGrid .game-card").count() === 1, "Library update filter works");
await Page.locator('[data-library-filter="all"]').click();
await Page.locator("#LibraryViewToggle").click();
Confirm(await Page.locator("#LibraryGrid").evaluate(Element => Element.classList.contains("list")), "Library list view works");
await Page.locator("#LibraryViewToggle").click();

await Page.locator('.nav-link[data-view="downloads"]').click();
Confirm(await Page.locator("#ViewDownloads").isVisible(), "Downloads navigation works");
Confirm(await Page.locator("#ActiveDownloads .download-card").count() === 1, "Active download card renders");
Confirm(await Page.locator("#QueuedDownloads .download-card").count() === 1, "Queued update renders separately");
Confirm(await Page.locator(".insight-card").count() === 2, "Speed and time insight cards render");
Confirm(await Page.locator(".downloads-rail .panel-card").count() === 2, "Storage and Preferences rail cards render");
Confirm((await Page.locator("#SpeedLine").getAttribute("points")).length > 0, "Speed graph has plotted samples");
Confirm(await Page.locator("#StorageCard").evaluate(Element => Element.classList.contains("ok") || Element.classList.contains("warning")), "Storage card exposes red/green status");
Confirm((await Page.locator("#StorageGamesValue").textContent()).includes("GB"), "Storage breakdown lists games usage");
Confirm((await Page.locator("#StorageSystemValue").textContent()).includes("GB"), "Storage breakdown lists system reserve");
await Page.locator('#BandwidthLimit [data-bandwidth="25"]').click();
Confirm(await Page.locator('#BandwidthLimit [data-bandwidth="25"]').getAttribute("aria-pressed") === "true", "Bandwidth segmented control selects 25 MB/s");
Confirm((await Page.locator("#BandwidthNote").textContent()).includes("25 MB/s"), "Bandwidth note reflects the selected limit");
await Page.locator("#SettingsButton").click();
Confirm(await Page.locator("#SpeedLimit").inputValue() === "25", "Preferences bandwidth syncs with settings dialog");
await Page.locator("#SettingsClose").click();
await Page.locator('#BandwidthLimit [data-bandwidth="0"]').click();
await Page.locator("#PrefAutoUpdate").uncheck();
Confirm(!(await Page.locator("#PrefAutoUpdate").isChecked()), "Preferences auto-update toggle works");
await Page.locator("#PrefAutoUpdate").check();
const MetricState = await Page.evaluate(() =>
{
    const CardStyle = getComputedStyle(document.querySelector(".insight-card"));
    return {
        Speed: Number(document.getElementById("SpeedValue").textContent),
        CardSpeed: parseFloat(document.querySelector("#ActiveDownloads .download-metrics strong").textContent),
        Time: document.getElementById("TimeLeftValue").textContent,
        Storage: Number(document.getElementById("StorageValue").textContent),
        CardBackground: CardStyle.backgroundColor,
        GridDisplay: getComputedStyle(document.querySelector(".insight-grid")).display
    };
});
Confirm(MetricState.Speed > 0 && Math.abs(MetricState.Speed - MetricState.CardSpeed) < 0.05, "Speed insight matches the active transfer card");
Confirm(MetricState.Time !== "—" && MetricState.Storage > 0, "Time-left and storage insights replace placeholder values");
Confirm(MetricState.GridDisplay === "grid" && MetricState.CardBackground !== "rgba(0, 0, 0, 0)", "Insight cards retain their grid and surface styling");
await Page.evaluate(() => { document.getElementById("Toast").hidden = true; });
await Page.screenshot({ path: path.join(ImageDirectory, "DownloadsActive.png"), fullPage: true });
const InitialProgress = await Page.locator("#ActiveDownloads .download-progress").getAttribute("aria-valuenow");
await Page.locator('[data-transfer-action="pause"]').click();
Confirm(await Page.locator("#ActiveDownloads .download-card").evaluate(Element => Element.classList.contains("paused")), "Pause control changes the card");
await Page.waitForTimeout(700);
const PausedProgress = await Page.locator("#ActiveDownloads .download-progress").getAttribute("aria-valuenow");
Confirm(PausedProgress === InitialProgress, "Paused transfer does not advance");
await Page.evaluate(() => { document.getElementById("Toast").hidden = true; });
await Page.screenshot({ path: path.join(ImageDirectory, "Downloads.png"), fullPage: true });
await Page.locator('[data-transfer-action="resume"]').click();
Confirm(!(await Page.locator("#ActiveDownloads .download-card").evaluate(Element => Element.classList.contains("paused"))), "Resume control restores the card");
await Page.locator("#PauseAll").click();
Confirm(await Page.locator("#PauseAll").textContent() === "Resume all", "Pause all switches to resume all");
await Page.locator("#PauseAll").click();

await Page.locator('.nav-link[data-view="store"]').click();
await Page.locator('#StoreGrid [data-game="control"]').click();
Confirm(await Page.locator("#GameDialog").isVisible(), "Game detail dialog opens");
Confirm(await Page.locator("#DialogTitle").textContent() === "Control", "Game detail dialog shows the selected game");
await Page.locator("#DialogAction").click();
Confirm((await Page.locator("#DialogAction").textContent()) === "Downloading", "Store purchase adds the game and starts a transfer");
await Page.locator("#DialogClose").click();
await Page.locator('.nav-link[data-view="downloads"]').click();
Confirm(await Page.locator("#QueuedDownloads .download-card").count() === 2, "New install waits behind the active transfer");
await Page.locator('#QueuedDownloads [data-transfer-id="control"][data-transfer-action="cancel"]').click();
Confirm(await Page.locator('#QueuedDownloads [data-transfer="control"]').count() === 0, "Queued transfer can be cancelled");

await Page.locator("#SettingsButton").click();
Confirm(await Page.locator("#SettingsDialog").isVisible(), "Settings dialog opens");
await Page.locator("#SpeedLimit").selectOption("10");
await Page.locator("#SettingsClose").click();
await Page.reload({ waitUntil: "networkidle" });
await Page.locator("#SettingsButton").click();
Confirm(await Page.locator("#SpeedLimit").inputValue() === "10", "Download limit persists across reload");
await Page.locator("#SettingsClose").click();

await Page.evaluate(() => localStorage.clear());
await Page.reload({ waitUntil: "networkidle" });
for (const [Width, Height] of [[980, 620], [820, 620], [780, 600]])
{
    await Page.setViewportSize({ width: Width, height: Height });
    await Page.locator('.nav-link[data-view="downloads"]').click();
    await Page.waitForTimeout(600);
    const NarrowState = await Page.evaluate(() => ({
        Overflow: document.documentElement.scrollWidth > document.documentElement.clientWidth,
        Columns: getComputedStyle(document.querySelector(".insight-grid")).gridTemplateColumns.split(" ").length,
        CardWidths: [...document.querySelectorAll(".insight-card")].map(Card => Math.round(Card.getBoundingClientRect().width)),
        Speed: Number(document.getElementById("SpeedValue").textContent)
    }));
    Confirm(!NarrowState.Overflow, `Split-preview downloads at ${Width}px have no horizontal overflow`);
    Confirm(NarrowState.Columns === 2 && NarrowState.CardWidths.every(WidthValue => WidthValue >= 160), `Split-preview insight cards stay in a readable grid at ${Width}px`);
    Confirm(NarrowState.Speed > 0, `Split-preview speed metric stays live at ${Width}px`);
    if (Width === 780)
    {
        await Page.evaluate(() => { document.getElementById("Toast").hidden = true; });
        await Page.screenshot({ path: path.join(ImageDirectory, "PreviewDownloads.png") });
    }
}
await Page.setViewportSize({ width: 390, height: 844 });
await Page.locator('.nav-link[data-view="store"]').click();
await Page.waitForTimeout(400);
const MobileOverflow = await Page.evaluate(() => document.documentElement.scrollWidth > document.documentElement.clientWidth);
Confirm(!MobileOverflow, "Mobile store has no horizontal overflow");
Confirm(await Page.locator(".primary-nav").isVisible(), "Mobile navigation remains reachable");
await Page.screenshot({ path: path.join(ImageDirectory, "MobileStore.png"), fullPage: true });
await Page.locator("#StoreDensityToggle").click();
await Page.waitForTimeout(300);
Confirm(!(await Page.evaluate(() => document.documentElement.scrollWidth > document.documentElement.clientWidth)), "Mobile compact game grid has no horizontal overflow");
await Page.locator("#StoreDensityToggle").click();

Confirm(ConsoleErrors.length === 0, "No browser console errors");
Confirm(PageErrors.length === 0, "No uncaught page errors");
await Browser.close();

const Report = {
    Date: new Date().toISOString(),
    Revision: "working tree on arena/d888bed1-frontier",
    Browser: "Chromium (serverless build), headless",
    Viewports: ["1440x960", "980x620", "820x620", "780x600", "390x844"],
    Checks,
    ConsoleErrors,
    PageErrors,
    Passed: Checks.every(Check => Check.Passed)
};
await writeFile(path.join(ResultDirectory, "ChromiumValidation.json"), JSON.stringify(Report, null, 2));
console.log(`${Checks.length}/${Checks.length} rendered checks passed`);
