//========================================================================================================================
//                                             SurfaceAttachment.js                                                🧩
//========================================================================================================================
//
// 📝 The host's Surface contract, presented over the ported editor.
//
//    RockFormationEntry.js was written against TreeSurface.js and reads five things off the object it gets
//    back: .Zoom for the readout, .Previews as an Identifier -> canvas Map, .OnEngage and .OnPreview as
//    callbacks, plus the four module functions RebuildSurface/RedrawLinks/FrameTree/EngageEntry.
//
//    🔴 EditorSurface.js EXPORTS NOTHING. It is NodeFlowEditor's EditorEntry.js byte-for-byte: a top-level
//       script that binds its elements at module scope and runs its own boot tail on import. So it cannot be
//       constructed, called, or handed a tree — it must be imported AFTER the DOM and the tree exist, and
//       everything the host needs from it has to be observed from the outside. That is what this file does.
//
//    🔴 Import ORDER is load-bearing and cannot be expressed as a static import. AttachTree must run before
//       EditorSurface.js evaluates, because its boot tail immediately reads RecordStore.Entries — which
//       throws the named "used before AttachTree" error if the tree is missing. A static
//       `import './EditorSurface.js'` would be hoisted above every statement in this file and always lose
//       that race, so the editor is brought in by a dynamic import inside AttachSurface below.

import { AttachTree, RecordStore }  from "./Graph/GraphStore.js";
import { PlaneState, Selection }    from "./Graph/EditorStore.js";
import { BindViewport }             from "./Terrain/TerrainViewport.js";
import { RasterizeEntry }           from "./Presentation/EntryRasterization.js";
import { ConstructionSpecificationTable } from "../Construction/ConstructionSpecifications.js";

// 📝 Held so the preview predicate can ask what species an identifier is without a second tree handle.
let AttachedTreeState = null;

//------------------------------------------------------------------------------------------------------------------------
//                                                  PREVIEW TRACKING
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 The host paints one thumbnail per card into a canvas it looks up by identifier, and the rasterizer
//    deliberately emits NO canvas — see the 🔴 note at EntryRasterization.js:198. That note is right: a
//    canvas authored inside the card markup is rebuilt by RefreshEntries() while the host still holds the
//    old element, so the thumbnail silently stops updating. So the canvas is INJECTED here instead, after
//    each rebuild, and the Map is re-pointed at the new element in the same pass. The rasterizer stays
//    verbatim and the host's Previews contract is still honoured.
//
//    🔴 A MutationObserver, not a wrapped RefreshEntries. RefreshEntries is module-private to the verbatim
//       editor and there is no seam to hook; observing the plane catches every rebuild however it was
//       provoked, including the ones inside drag and marquee handling that never call the obvious path.

const ThumbnailEdge = 96;                                           // [px] matches the host's preview render

const Previews = new Map();                                         // Identifier -> canvas

function HarvestPreviews(EntryPlane, Surface)
{
    Previews.clear();

    for (const Card of EntryPlane.querySelectorAll(".NodeEntry[data-entry]"))
    {
        const Identifier = Number(Card.dataset.entry);
        if (!Number.isFinite(Identifier)) continue;

        // 🔴 Only previewable species get a canvas. Asking the host to paint a weather process hides the
        //    canvas at DrainPreviewQueue's `if (!Painted)` — a reserved grey hole on every such card.
        if (!PreviewableCondition(Identifier)) continue;

        const Interior = Card.querySelector(".EntryBody") || Card;

        // 📝 A folded card keeps no interior rows, but the canvas must still exist or the host treats the
        //    entry as gone and never repaints it once unfolded.
        let Canvas = Interior.querySelector(":scope > canvas.EntryPreview");
        if (!Canvas)
        {
            Canvas = document.createElement("canvas");
            Canvas.className   = "EntryPreview";
            Canvas.width       = ThumbnailEdge;
            Canvas.height      = ThumbnailEdge;
            Canvas.title       = "preview";

            // 🔴 No `painted` flag is set here. The host repaints any unmarked canvas regardless of its
            //    stamp (RockFormationEntry.js:479), which is exactly what a fresh blank element needs.
            Canvas.addEventListener("pointerdown", Event => Event.stopPropagation());
            Canvas.addEventListener("click", Event =>
            {
                Event.stopPropagation();                            // 🔴 or the click also re-selects the card
                if (Surface.OnPreview) Surface.OnPreview(Identifier);
            });

            Interior.appendChild(Canvas);
        }

        Previews.set(Identifier, Canvas);
    }
}

// 📝 The same rule the old surface used: everything except the Resolve family carries a thumbnail.
function PreviewableCondition(Identifier)
{
    const Entry = AttachedTreeState && AttachedTreeState.Entries.get(Identifier);
    if (!Entry) return false;
    const Specification = ConstructionSpecificationTable[Entry.Species];
    return !!Specification && Specification.Family !== "Resolve";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ATTACHMENT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Returns the object the host already knows how to drive.
export async function AttachSurface(TreeState, ViewProfile, Notify, ApplyScale)
{
    // 🔴 Both bindings BEFORE the editor is imported — see the ordering note at the head of this file.
    AttachedTreeState = TreeState;
    AttachTree(TreeState);
    BindViewport(ViewProfile, Notify, ApplyScale);

    const Surface =
    {
        TreeState  : TreeState,
        Notify     : Notify,
        Previews   : Previews,
        OnEngage   : null,                                          // set by the host
        OnPreview  : null,                                          // set by the host

        // 📝 The host reads Zoom for its readout; the editor keeps the real value on the plane state.
        get Zoom()      { return PlaneState.PlaneScale; },
        set Zoom(Value) { PlaneState.PlaneScale = Value; }
    };

    // ⚠️ Awaited, so everything below observes a booted editor rather than an empty plane.
    await import("./EditorSurface.js");

    const EntryPlane = document.getElementById("EntryPlane");
    if (!EntryPlane) throw new Error("AttachSurface: #EntryPlane is absent — the shell did not load");

    HarvestPreviews(EntryPlane, Surface);

    // 🔴 Every rebuild discards the canvases and creates blank ones, so the thumbnails must be re-injected
    //    and rescheduled on EVERY rebuild — not only when the selection moved. OnEngage is the host's
    //    "repaint whatever is stale" entry point and it is already stamp-guarded (RockFormationEntry.js:479),
    //    so calling it unconditionally costs nothing when nothing actually changed.
    const Watcher = new MutationObserver(() =>
    {
        HarvestPreviews(EntryPlane, Surface);
        if (Surface.OnEngage) Surface.OnEngage();
    });

    Watcher.observe(EntryPlane, { childList: true });

    return Surface;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              THE FOUR MODULE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 The host calls these by name. The editor owns the real implementations privately, so each is expressed
//    through a public effect: a synthetic event the editor already listens for, or a direct DOM read.
//
//    🔴 None of these may call Notify. The host calls them FROM its Notify handler, so a callback here would
//       be a recompile loop that renders correctly and pegs a core.

// 🔴 MEASURED, not assumed: the editor's only two window listeners are 'keydown' (1096) and 'resize'
//    (1505), and the resize handler body is `InscribeLinks()` alone. So dispatching a resize RELINKS but
//    does NOT rebuild cards — RefreshEntries/RefreshEditor are module-private with no export and no global,
//    so there is genuinely no public seam that rebuilds. A RebuildSurface that only dispatched 'resize'
//    would silently do half its job: after a topology change the links would move to their new anchors
//    while the cards themselves stayed stale, which reads as a routing bug rather than a missing rebuild.
//
//    📝 So a rebuild is performed here, in the reference's own two steps and the reference's own order —
//       replace the children from RecordStore.Entries, then relink. The observer below still covers the
//       rebuilds the editor provokes internally; this covers the ones the host asks for.

function RebuildEntryPlane()
{
    const EntryPlane = document.getElementById("EntryPlane");
    if (!EntryPlane) return;

    while (EntryPlane.firstChild) EntryPlane.removeChild(EntryPlane.firstChild);
    for (const Entry of RecordStore.Entries) EntryPlane.appendChild(RasterizeEntry(Entry));
}

export function RebuildSurface(Surface)
{
    RebuildEntryPlane();
    // 📝 The mutation observer re-injects the preview canvases and relinks via the resize below.
    window.dispatchEvent(new Event("resize"));
}

// 📝 Links only — the anchors are read from the live DOM, so no rebuild is needed to re-route them.
export function RedrawLinks(Surface)
{
    window.dispatchEvent(new Event("resize"));
}

// 📝 'f' frames the tree — the editor's own keyboard shortcut, dispatched rather than reimplemented so the
//    easing and padding stay whatever the reference chose. Verified live at EditorSurface.js:1118.
export function FrameTree(Surface)
{
    window.dispatchEvent(new KeyboardEvent("keydown", { key: "f", bubbles: true }));
}

// 📝 Selecting a card is a click on its head in the reference, so the selection set is written directly and
//    the plane rebuilt — far less fragile than synthesising a pointer sequence at the right coordinate.
export function EngageEntry(Surface, Identifier)
{
    Selection.clear();
    if (Identifier !== undefined && Identifier !== null) Selection.add(Number(Identifier));
    RebuildSurface(Surface);
}
