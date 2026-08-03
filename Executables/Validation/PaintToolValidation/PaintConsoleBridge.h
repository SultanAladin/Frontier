/*==============================================================================================================================================
                                                        PAINTCONSOLEBRIDGE.H
==============================================================================================================================================*/
// 🧩 The translation layer between the texture-paint workspace and the shared console. Third of three, and the one that exercises BOTH of the
//    console's extension points rather than just the gate: paint is the workspace the console had to grow surface painters for.
//
//    🔴 Paint has NO gate, and that is a finding rather than an omission. All 102 instruments are always selectable — the availability question the
//       modelling and construction workspaces answer per action simply does not arise here. So this binds NO VerdictResolver, and the console's
//       documented null-resolver rule (every action resolves Live) is exactly the right behaviour. Nothing is invented to fill the slot.
//
//    🔴 What paint DOES need is to draw two regions the console cannot express, and neither may be dropped:
//      * the SLIDE-2 LEFT COLUMN is a live stroke preview — 220 procedurally stamped dabs over pale paper, the standing instrument, and the ink
//        swatch strip — not a table of readings. Bound as SurfaceBinding::PaintColumn.
//      * each ACTION TILE's artwork is a round well holding the instrument's own SVG nib crop (or its full landscape strip, per art mode), sampled
//        from this app's registry and lazy strip store. Bound as SurfaceBinding::PaintTile.
//    Both keep the existing, already-ported painters in PaintPreviewColumn/PaintInstrumentGrid doing the actual drawing — the bridge only maps the
//    console's region onto the arguments they already take. That is what "every workspace draws its own panel" buys: paint's surfaces survive intact
//    and no paint-only concept enters the shared module.
//
//    🔴 Paint's options are RESOLVED PER FRAME, not authored in a constexpr table. An edit can add or remove rows (a Custom grade reveals hardness),
//       so the bridge rebuilds a ParameterDescriptor arena each frame from the live schema. The arena is owned by the context and the descriptor
//       borrows it, which is why the context must outlive the console call — the same contract the other two bridges have.

#pragma once
#ifndef FRONTIER_VALIDATION_PAINTTOOL_PAINTCONSOLEBRIDGE_H
#define FRONTIER_VALIDATION_PAINTTOOL_PAINTCONSOLEBRIDGE_H

#include "EngineContext/Interface/WorkspaceContextConsole/Descriptor/WorkspaceContextConsoleDescriptor.h"
#include "EngineContext/Interface/WorkspaceContextConsole/Descriptor/ParameterDescriptor.h"
#include "EngineContext/Interface/WorkspaceContextConsole/Pane/ParameterPane.h"

#include "PaintCardSpecification.h"
#include "PaintInstrumentGrid.h"
#include "PaintPreviewColumn.h"
#include "PaintSchema.h"

namespace Frontier
{

struct SvgIconRegistry;
struct PaintIconStore;

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything the two painters and the per-frame arena read, handed to the console as SurfaceBinding::Context. Unlike the other two bridges this is
//    NOT the gate's input (paint has no gate) — it is the drawing context, which is why the console takes it as void* rather than const void*: the
//    strip store populates lazily, so painting an unseen instrument legitimately mutates it.
struct PaintConsoleContext
{
    //---- what the column paints ----
    const PaintCardPalette* CardPalette     = nullptr;   // [-]  - paint's own tokens (paper, well ring); the shared palette has no field for them
    const PaintCardMetrics* CardMetrics     = nullptr;   // [-]  - paint's 560x420 geometry
    PaintPreviewState*      Preview         = nullptr;   // [-]  - which ink; written when a swatch is clicked (borrowed)
    PaintStrokeParameters   Stroke          = {};        // [-]  - the six values the stamp compositor reads, resolved by the panel
    int                     InstrumentIndex = -1;        // [idx]- the open instrument, into the whole catalogue
    int                     VisibleGroupCount = 0;       // [idx]- for the column's spec rows
    int                     ParameterCount    = 0;       // [idx]- for the column's spec rows

    //---- what the tiles paint ----
    SvgIconRegistry*        Registry   = nullptr;        // [-]  - square nib crops, registered up front (borrowed)
    PaintIconStore*         StripStore = nullptr;        // [-]  - landscape strips, uploaded lazily (borrowed)
    PaintWellArtMode        ArtMode    = PaintWellArtMode::NibCrop;

    //---- the per-frame options arena ----
    // 🔴 Rebuilt every frame from the live schema, because a reveal changes the row set. The descriptor borrows this, so the context must outlive the
    //    console call. Sized at the console's own block limit: a schema resolving more rows than the pane can bind is clamped, not overrun.
    ParameterDescriptor     Rows[ConsoleParameterBlockLimit] = {};
    int                     RowCount = 0;                // [idx]- rows actually built this frame

    //---- what the console reports back through the painters ----
    // 📝 The swatch click cannot ride the ConsoleResult — the console has no vocabulary for "an ink was chosen" and should not grow one. So the column
    //    painter latches it here and the panel drains it after the console returns.
    int                     ClickedSwatch = -1;          // [idx]- swatch the column reported this frame; -1 for none
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Refresh the context for this frame: records the live stroke parameters and rebuilds the options arena from the visible control list. Call before
// composing, every frame — the arena is what the reveals flow through.
void BindPaintConsoleContext(PaintConsoleContext&       Context,
                             const PaintVisibleControl* Visible,
                             int                        VisibleCount,
                             const PaintControlValue*   Values,
                             int                        ValueCount);

// The console descriptor for the paint catalogue: the cluster/action tables (built once from the authored families and instruments), paint's own
// geometry, both surface painters, and NO gate.
WorkspaceContextConsoleDescriptor ComposePaintConsoleDescriptor(PaintConsoleContext& Context);

// Map a console (cluster, action) pair back onto a catalogue instrument index, or -1. The console reports positions in its own tables; the panel
// needs the catalogue index the rest of paint is keyed by.
[[nodiscard]] int ResolvePaintInstrumentFor(int ClusterIndex, int ActionIndex);

// The console cluster + action a catalogue instrument sits at, for restoring focus after a selection made outside the console.
void ResolvePaintConsolePosition(int InstrumentIndex, int& ClusterIndex, int& ActionIndex);

} // namespace Frontier

#endif
