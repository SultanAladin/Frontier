// HMI demo wiring (HmiDemo.html): paint only. The viewport is IlluminationHost's ReSTIR Lobby render of the
// single hero (?hmi=1 — see FacetStructure); this module only recolours it through the same VehicleAppearance
// the in-cabin tablet uses, so the finish lands on the live, driven vehicle via the shared material records.
import { PaintOptions } from "./VehicleAppearance.js";

const Panel = document.getElementById("swatches");
const Status = document.getElementById("paint-status");
const Ghost = document.getElementById("drag-ghost");
const Canvas = document.getElementById("viewport");
let Selected = null, Dragging = null;

function Host() { return globalThis.IlluminationExhibit || null; }
function Apply(Paint, Via) {
    const H = Host();
    if (!H?.Cabin?.Appearance) { Status.textContent = "Renderer still starting — try again in a moment."; return; }
    H.Cabin.Appearance.Apply({ Colour: Paint.Hex });
    Selected = Paint;
    for (const B of Panel.children) B.setAttribute("aria-pressed", String(B.dataset.name === Paint.Name));
    Status.textContent = `${Paint.Name} applied to the live vehicle${Via ? " (" + Via + ")" : ""}.`;
}
for (const Paint of PaintOptions) {
    const Button = document.createElement("button");
    Button.className = "swatch"; Button.dataset.name = Paint.Name; Button.setAttribute("aria-pressed", "false");
    Button.innerHTML = `<span class="chip" style="background:${Paint.Hex}"></span><span class="name">${Paint.Name}</span>`;
    Button.addEventListener("click", () => Apply(Paint, "click"));
    Button.addEventListener("pointerdown", (E) => { Dragging = Paint; Ghost.style.background = Paint.Hex; Ghost.classList.remove("hidden"); MoveGhost(E); });
    Panel.appendChild(Button);
}
function MoveGhost(E) { Ghost.style.left = (E.clientX - 22) + "px"; Ghost.style.top = (E.clientY - 22) + "px"; }
addEventListener("pointermove", (E) => { if (Dragging) MoveGhost(E); });
addEventListener("pointerup", (E) => {
    if (!Dragging) return;
    const Paint = Dragging; Dragging = null; Ghost.classList.add("hidden");
    // A drop anywhere on the rendered car (the canvas fills the stage) applies; a drop off-stage cancels.
    if (E.target === Canvas) Apply(Paint, "drag onto the car");
});
// The lobby defaults to a dark studio colour at bring-up; mark Nebula Silver (the reference default) once ready.
const Timer = setInterval(() => {
    if (Host()?.Ready) { clearInterval(Timer); const First = PaintOptions[0];
        for (const B of Panel.children) B.setAttribute("aria-pressed", String(B.dataset.name === First.Name));
        Selected = First; Status.textContent = "Ready — one vehicle, paint only. Drag to orbit, scroll to zoom."; }
}, 250);
