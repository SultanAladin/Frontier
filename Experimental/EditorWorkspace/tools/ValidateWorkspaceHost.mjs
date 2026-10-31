import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { JSDOM } from "jsdom";

const Prototype = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const Html = fs.readFileSync(`${Prototype}/index.html`, "utf8");

const ConsoleErrors = [];
const Dom = new JSDOM(Html, { url: "http://localhost/", runScripts: "outside-only", pretendToBeVisual: true });
const Window = Dom.window;

Window.HTMLCanvasElement.prototype.getContext = () => null;
Window.Element.prototype.setPointerCapture = () => {};
Window.Element.prototype.releasePointerCapture = () => {};
Window.Element.prototype.hasPointerCapture = () => false;
Window.requestAnimationFrame = () => 1;
Window.cancelAnimationFrame = () => {};

globalThis.window = Window;
globalThis.document = Window.document;
// navigator already exists on recent Node runtimes.
globalThis.Element = Window.Element;
globalThis.HTMLElement = Window.HTMLElement;
globalThis.HTMLCanvasElement = Window.HTMLCanvasElement;
globalThis.SVGElement = Window.SVGElement;
globalThis.Event = Window.Event;
globalThis.MouseEvent = Window.MouseEvent;
globalThis.KeyboardEvent = Window.KeyboardEvent;
globalThis.requestAnimationFrame = () => 1;
globalThis.cancelAnimationFrame = () => {};

let FailureTotal = 0, CheckTotal = 0;
function Check(Label, Condition, Detail = "") {
    CheckTotal += 1;
    if (Condition) { console.log(`PASS  ${Label} ${Detail}`); return; }
    FailureTotal += 1;
    console.error(`FAIL  ${Label} ${Detail}`);
}
const Query = (Id) => Window.document.getElementById(Id);
const Click = (Node) => Node.dispatchEvent(new Window.MouseEvent("click", { bubbles: true, cancelable: true }));
const Rows = () => Array.from(Query("TreeRows").querySelectorAll(".tree-row"));
const RowNames = () => Rows().map((Row) => Row.querySelector(".tree-name").textContent);

try {
    await import(`${Prototype}/scripts/WorkspaceHost.js?run=${Date.now()}`);
    Check("workspace module initializes without throwing", true);
} catch (Error) {
    Check("workspace module initializes without throwing", false, String(Error && Error.stack).slice(0, 900));
}

Check("outliner renders every scene record", Rows().length === 5, `rows=${Rows().length} names=${RowNames().join(", ")}`);
Check("first record is selected", Rows()[0].classList.contains("is-selected"), `class="${Rows()[0].className}"`);
Check("inspector mirrors the selection", Query("InspectorName").textContent === "Knot study" && Query("InspectorType").textContent === "Topology Knot",
    `name=${Query("InspectorName").textContent} type=${Query("InspectorType").textContent}`);
Check("placement fields read live values", Query("PositionY").value === "0.55" && Query("ScaleX").value === "1.00",
    `y=${Query("PositionY").value} s=${Query("ScaleX").value}`);
Check("hero counts describe the scene", Query("HeroRecordCount").textContent === "5 records" && Query("TreeGroupCount").textContent === "5");
Check("diagnostic toast reports missing WebGL2 rather than failing silently",
    Query("DiagnosticToast").hidden === false && /WebGL2/.test(Query("DiagnosticToast").textContent));

Click(Rows()[1]);
Check("clicking a record transfers selection", Query("InspectorName").textContent === "Orbit ring" && Rows()[1].classList.contains("is-selected"),
    `name=${Query("InspectorName").textContent}`);
Check("selection frame is cleared without rasterization", Query("SelectionFrameBrackets").childElementCount === 0);

Click(Query("FilterRow").querySelector('[data-filter="geometry"]'));
Check("geometry filter isolates surface records", Rows().length === 2, RowNames().join(", "));
Click(Query("FilterRow").querySelector('[data-filter="light"]'));
Check("light filter isolates luminous records", Rows().length === 1 && RowNames()[0] === "Key light", RowNames().join(", "));
Click(Query("FilterRow").querySelector('[data-filter="all"]'));
Check("filter reset restores every record", Rows().length === 5);

Query("OutlinerSearch").value = "knot";
Query("OutlinerSearch").dispatchEvent(new Window.Event("input", { bubbles: true }));
Check("search narrows the directory", Rows().length === 1 && RowNames()[0] === "Knot study", RowNames().join(", "));
Query("OutlinerSearch").value = "";
Query("OutlinerSearch").dispatchEvent(new Window.Event("input", { bubbles: true }));
Check("clearing search restores the directory", Rows().length === 5);

Click(Query("AddRecordButton"));
Check("primitive menu opens", Query("PrimitiveMenu").hidden === false);
Click(Query("PrimitiveMenu").querySelector('[data-primitive="sphere"]'));
Check("adding a primitive appends a record", Rows().length === 6 && Query("InspectorName").textContent.includes("Sphere"),
    `rows=${Rows().length} name=${Query("InspectorName").textContent}`);
Check("added record is selected", Rows()[5].classList.contains("is-selected"));

Click(Query("DuplicateRecordButton"));
Check("duplicate appends a copied record", Rows().length === 7 && Query("InspectorName").textContent.endsWith("copy"),
    `rows=${Rows().length} name=${Query("InspectorName").textContent}`);

Window.dispatchEvent(new Window.KeyboardEvent("keydown", { key: "Delete", bubbles: true }));
Check("delete removes the selected record", Rows().length === 6, `rows=${Rows().length}`);

Click(Query("InspectorPanel").querySelector('[data-inspector-tab="surface"]'));
Check("surface tab reveals optics controls",
    Query("SurfaceSection").hidden === false && Query("PlacementSection").hidden === true);
Click(Query("InspectorPanel").querySelector('[data-inspector-tab="view"]'));
Check("view tab reveals camera controls", Query("ViewSection").hidden === false && Query("SurfaceSection").hidden === true);
Click(Query("InspectorPanel").querySelector('[data-inspector-tab="object"]'));

Click(Query("OutlinerPanel").querySelector('[data-outliner-tab="library"]'));
Check("library tab reveals primitive cards", Query("LibraryTabContent").hidden === false && Query("SceneTabContent").hidden === true);
Click(Query("OutlinerPanel").querySelector('[data-outliner-tab="scene"]'));
Check("scene tab returns to the directory", Query("SceneTabContent").hidden === false);

Query("PositionX").value = "1.25";
Query("PositionX").dispatchEvent(new Window.Event("change", { bubbles: true }));
Check("numeric placement field commits", Query("PositionX").value === "1.25", `value=${Query("PositionX").value}`);

const SliderRow = Window.document.querySelector('[data-surface-property="RimStrength"]');
const SliderTrack = SliderRow.querySelector(".slider-track");
SliderTrack.getBoundingClientRect = () => ({ left: 0, width: 200, top: 0, height: 18, right: 200, bottom: 18 });
SliderRow.dispatchEvent(new Window.MouseEvent("pointerdown", { bubbles: true, cancelable: true, clientX: 100 }));
SliderRow.dispatchEvent(new Window.MouseEvent("pointerup", { bubbles: true, cancelable: true, clientX: 100 }));
Check("surface slider maps pointer position to a value", SliderRow.querySelector(".slider-value").textContent === "1.20",
    `value=${SliderRow.querySelector(".slider-value").textContent}`);

Query("OutlinerPanel").getBoundingClientRect = () => ({ left: 0, top: 0, width: 308, height: 800, right: 308, bottom: 800 });
const Splitter = Query("DockRow").querySelector('[data-splitter="Outliner"]');
Splitter.dispatchEvent(new Window.MouseEvent("pointerdown", { bubbles: true, cancelable: true, clientX: 300 }));
Splitter.dispatchEvent(new Window.MouseEvent("pointermove", { bubbles: true, cancelable: true, clientX: 360 }));
Splitter.dispatchEvent(new Window.MouseEvent("pointerup", { bubbles: true, cancelable: true, clientX: 360 }));
Check("outliner splitter writes a dock width", Query("DockRow").style.getPropertyValue("--Outliner-Width") === "368px",
    `value=${Query("DockRow").style.getPropertyValue("--Outliner-Width")}`);

Click(Query("OutlinerPanel").querySelector('[data-panel-action="collapse"]'));
Check("outliner collapses to a rail", Query("OutlinerPanel").classList.contains("is-collapsed"));
Click(Query("OutlinerPanel").querySelector('[data-panel-action="expand"]'));
Check("outliner expands again", !Query("OutlinerPanel").classList.contains("is-collapsed"));

Click(Query("GridToggle"));
Check("grid toggle reflects state", Query("GridToggle").classList.contains("is-active") === false);

Click(Query("TimelinePlay"));
Check("timeline transport reports paused", Query("TimelinePlay").classList.contains("is-playing") === false);
Check("timeline ruler is populated", Query("TimelineRuler").childElementCount === 25, `ticks=${Query("TimelineRuler").childElementCount}`);

Window.dispatchEvent(new Window.KeyboardEvent("keydown", { key: "w", bubbles: true }));
Check("transform shortcut activates move tool", Query("ToolRibbon").querySelector('[data-tool="move"]').classList.contains("is-active"));
Window.dispatchEvent(new Window.KeyboardEvent("keydown", { key: "g", bubbles: true }));
Check("grid shortcut restores the grid", Query("GridToggle").classList.contains("is-active"));

const ViewModeChip = Query("ViewModeChip");
Click(ViewModeChip);
Check("view mode chip switches to orthographic", ViewModeChip.textContent.trim().startsWith("Orthographic"));
Click(ViewModeChip);
Check("view mode chip returns to perspective", ViewModeChip.textContent.trim().startsWith("Perspective"));

const SurfaceModeChip = Query("SurfaceModeChip");
Click(SurfaceModeChip);
Check("surface chip advances the shading preset", SurfaceModeChip.textContent.trim().startsWith("Wireframe"),
    `label=${SurfaceModeChip.textContent.trim()}`);
Click(SurfaceModeChip);
Click(SurfaceModeChip);
Check("surface chip cycles back to holographic", SurfaceModeChip.textContent.trim().startsWith("Holographic"));

Query("ViewSection").querySelector('[data-view-toggle="SelectionAxes"]').dispatchEvent(new Window.MouseEvent("click", { bubbles: true }));
Check("selection axis switch toggles its state",
    Query("ViewSection").querySelector('[data-view-toggle="SelectionAxes"]').classList.contains("is-on") === false);

const RateChips = Query("TimelineRate").querySelectorAll("span");
Click(RateChips[2]);
Check("timeline rate chip becomes active", RateChips[2].classList.contains("is-active") && !RateChips[1].classList.contains("is-active"));

console.log(`\nChecks executed: ${CheckTotal}`);
console.log(`Failures: ${FailureTotal}`);
if (FailureTotal > 0) { process.exitCode = 1; }
