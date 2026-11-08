/*==========================================================================================================================================
                                                        VOLUMESCULPTINTERACTION.JS
==========================================================================================================================================*/
// 🧩 Bounded asynchronous XYZ brush picking, region-locked atomic strokes and a world-radius cursor; no image rendering.

import { SculptBrushCentre, MaximumSculptSamples } from './VolumeSculpt.js';
import { ProjectVolumeCamera } from './VolumeRayField.js';

export function InitializeVolumeSculpting(API)
{
    const Find = Selector => document.querySelector(Selector), Canvas = API.Canvas;
    let Enabled = false, Opening = false, Epoch = 0, Stroke = null, Pending = null, Picking = false;
    const SculptingActive = () => API.RetrieveWorkspace() === 'landscape' && Enabled;
    function Refresh()
    {
        const Selection = API.RetrieveSelection(), Available = Selection.Region && Selection.Layer.Visible && API.RetrieveWorkspace() === 'landscape';
        Find('#SculptPanel').hidden = API.RetrieveWorkspace() !== 'landscape';
        Find('#SculptTool').disabled = Find('#SculptEnable').disabled = !Available;
        Find('#SculptTool').classList.toggle('active', Enabled);
        Find('#SculptEnable').textContent = Opening ? 'Focusing high-detail surface…' : Enabled ? 'Finish sculpting' : 'Start 3D sculpting';
        Find('#SculptTool').setAttribute('aria-pressed', String(Enabled));
        Find('#SculptScope').textContent = Available ? `${Selection.Layer.Name} · region ${Selection.RegionIndex + 1} only · ${Selection.Region.SculptStrokes?.length || 0} saved strokes` : 'Select a visible, allocated region.';
        Find('#SculptRadiusRead').textContent = `${Number(Find('#SculptRadius').value).toFixed(1)} m`;
        Find('#SculptStrengthRead').textContent = `${Math.round(Number(Find('#SculptStrength').value) * 100)}%`;
        Canvas.dataset.sculptEnabled = String(Enabled);
    }
    function Cancel()
    {
        Epoch++; Stroke = null; Pending = null;
        Canvas.dataset.sculptPending = 'false';
        Find('#SculptCursor').hidden = true;
    }
    function Disable()
    {
        Cancel(); Enabled = false; Opening = false;
        Canvas.style.cursor = 'default'; Refresh();
    }
    async function Toggle()
    {
        if (Enabled) { Disable(); return; }
        if (!API.RetrieveSelection().Region || API.RetrieveWorkspace() !== 'landscape') return;
        Cancel(); Enabled = true; Opening = true;
        const Token = Epoch;
        API.ShowInspector(); Refresh();
        await API.EnterDetail();
        if (Token !== Epoch || API.RetrieveWorkspace() !== 'landscape') return;
        Opening = false; Canvas.style.cursor = 'crosshair'; Refresh();
        API.Report('3D sculpt: left-drag clay/carve; release to apply one undoable stroke. Alt-drag orbits; Shift/MMB pans. Other layers are protected.');
    }
    function ProjectCursor(Client, Hit)
    {
        const Bounds = Canvas.getBoundingClientRect(), Projection = ProjectVolumeCamera(API.Camera);
        const Depth = Hit.Position.reduce((Sum, Coordinate, Axis) => Sum + (Coordinate - Projection.Eye[Axis]) * Projection.Forward[Axis], 0);
        const Diameter = Math.max(6, Math.min(Bounds.height * 2, Number(Find('#SculptRadius').value) * 1.65 * Bounds.height / Math.max(.01, Depth)));
        const Cursor = Find('#SculptCursor');
        Cursor.hidden = false; Cursor.style.left = `${Client[0] - Bounds.left}px`; Cursor.style.top = `${Client[1] - Bounds.top}px`;
        Cursor.style.width = Cursor.style.height = `${Diameter}px`;
        Cursor.dataset.mode = Stroke?.Mode || Find('#SculptMode').value;
    }
    function Current(Part)
    {
        const Selection = API.RetrieveSelection();
        return SculptingActive() && Part.Revision === API.RetrieveRevision() && Selection.Layer === Part.Layer &&
            Selection.Region === Part.Region && Part.Layer.Visible;
    }
    function FinalizeStroke()
    {
        const Finished = Stroke;
        if (!Finished?.Released || Picking || Pending) return;
        Stroke = null;
        if (!Current(Finished)) { Canvas.dataset.sculptPending = 'false'; return; }
        const Parts = Finished.Parts.filter(Points => Points.length).map(Points => ({ Mode: Finished.Mode, Radius: Finished.Radius, Strength: Finished.Strength, Points }));
        if (!Parts.length) API.Report('No surface on the active region was hit; no sculpt edit was made.');
        else API.FinalizeStroke(Finished, Parts);
        Canvas.dataset.sculptPending = 'false'; Refresh();
    }
    async function Drain()
    {
        if (Picking) return;
        Picking = true;
        while (Pending)
        {
            const Request = Pending; Pending = null;
            const Bounds = Canvas.getBoundingClientRect();
            try
            {
                const Hit = await API.Inspect({ Category: 'pick', Camera: structuredClone(API.Camera),
                    X: (Request.Client[0] - Bounds.left) / Bounds.width, Y: (Request.Client[1] - Bounds.top) / Bounds.height });
                if (Request.Epoch !== Epoch || !SculptingActive()) continue;
                const Selection = API.RetrieveSelection();
                if (!Hit.Found || Hit.LayerIndex !== Selection.LayerIndex || Hit.RegionIndex !== Selection.RegionIndex)
                {
                    Find('#SculptCursor').hidden = true;
                    if (Stroke?.Parts.at(-1)?.length) Stroke.Parts.push([]);
                    continue;
                }
                ProjectCursor(Request.Client, Hit);
                if (!Stroke || !Current(Stroke)) continue;
                const Point = SculptBrushCentre(Hit.Position, Hit.Normal, Stroke.Radius, Stroke.Strength, Stroke.Mode);
                let Points = Stroke.Parts.at(-1), Last = Points.at(-1);
                if (Last && Math.hypot(...Point.map((Coordinate, Axis) => Coordinate - Last[Axis])) > Stroke.Radius * 4)
                { Stroke.Parts.push([]); Points = Stroke.Parts.at(-1); Last = null; }
                if ((!Last || Math.hypot(...Point.map((Coordinate, Axis) => Coordinate - Last[Axis])) >= Stroke.Radius * .12) &&
                    Stroke.Parts.reduce((Count, Part) => Count + Part.length, 0) < 256) Points.push(Point);
            }
            catch (Failure)
            {
                if (Request.Epoch === Epoch) { API.Report(`Sculpt pick stopped: ${Failure.message}`); Cancel(); }
            }
        }
        Picking = false; FinalizeStroke();
    }
    function Queue(Event)
    {
        Pending = { Client: [Event.clientX, Event.clientY], Epoch };
        void Drain();
    }
    function PointerDown(Event, Navigate)
    {
        if (!SculptingActive() || Navigate || Event.button !== 0) return false;
        Event.preventDefault();
        if (Opening) return true;
        const Selection = API.RetrieveSelection();
        if (!Selection.Region || !Selection.Layer.Visible) return true;
        if (Stroke) return true;
        Stroke = { ...Selection, Revision: API.RetrieveRevision(), Mode: Find('#SculptMode').value,
            Radius: Number(Find('#SculptRadius').value), Strength: Number(Find('#SculptStrength').value), Parts: [[]], Released: false };
        Canvas.dataset.sculptPending = 'true'; Queue(Event); return true;
    }
    function PointerMove(Event)
    {
        if (!SculptingActive() || Opening) return;
        Queue(Event);
    }
    function PointerUp(Event)
    {
        if (!Stroke) return;
        Stroke.Released = true; Queue(Event); FinalizeStroke();
    }
    Find('#SculptTool').onclick = Find('#SculptEnable').onclick = () => void Toggle();
    for (const Name of ['Radius', 'Strength', 'Mode']) Find('#Sculpt' + Name).oninput = Refresh;
    Find('#OrbitCamera').addEventListener('click', Disable); Find('#PanCamera').addEventListener('click', Disable);
    Canvas.addEventListener('pointerleave', () => { if (!Stroke) Find('#SculptCursor').hidden = true; });
    window.addEventListener('blur', Cancel);
    Refresh();
    return { Refresh, Toggle, Disable, Cancel, PointerDown, PointerMove, PointerUp, Enabled: () => Enabled, MaximumSamples: MaximumSculptSamples };
}
