# Vehicle inspector page validation

Generated 2026-11-03T20:11:43.940Z · Node v22.22.3 · jsdom 30.1.3 · 94/94 checks passed.

The harness builds the real `index.html` in jsdom, installs a ResizeObserver and a recording 2D canvas context,
then evaluates `VehicleCatalogue.js`, `InspectorControls.js` and `VehicleInspector.js` in page order.
It clicks the real buttons, presses real arrow keys and reads the real DOM back.

| Check | Result | Observed |
|-------|--------|----------|
| no uncaught error while building | ✔️ pass | none |
| outliner lists ten records | ✔️ pass | 10 rows |
| outliner group · Rendering | ✔️ pass | Rendering |
| outliner group · Environment | ✔️ pass | Environment |
| outliner group · Vehicle | ✔️ pass | Vehicle |
| outliner group · Pit lane | ✔️ pass | Pit lane |
| vehicle record is selectable | ✔️ pass | Vehicle · GT-04 |
| four tyre records are nested under the vehicle | ✔️ pass | 4 nested |
| section · Identity | ✔️ pass | #Section-Identity |
| section · Engine | ✔️ pass | #Section-Engine |
| section · Suspension | ✔️ pass | #Section-Suspension |
| section · SoftBody | ✔️ pass | #Section-SoftBody |
| section · Curves | ✔️ pass | #Section-Curves |
| section · Thermal | ✔️ pass | #Section-Thermal |
| section · Tyres | ✔️ pass | #Section-Tyres |
| section · Aero | ✔️ pass | #Section-Aero |
| section · Simulation | ✔️ pass | #Section-Simulation |
| section · Performance | ✔️ pass | #Section-Performance |
| section · Record | ✔️ pass | #Section-Record |
| section tabs match the sections | ✔️ pass | 11 tabs |
| inspector title names the record | ✔️ pass | Vehicle · GT-04 |
| hero shows peak torque with its unit | ✔️ pass | 640Nm |
| hero side shows peak power | ✔️ pass | 371kW@ 6575 rpm |
| hero chart drew both dyno series | ✔️ pass | 3 paths |
| hero chart drew two torques and power axis | ✔️ pass | axes drawn |
| identity cards carry units | ✔️ pass | 4 cards |
| wheelbase card reads 2720 mm | ✔️ pass | 2720mmWheelbase |
| the summary row leads with the kerb mass | ✔️ pass | 1284kgKerb mass |
| the scene schematic drew the chassis | ✔️ pass | 240 canvas calls |
| the scene schematic labelled the tyres | ✔️ pass | fillText used |
| curve library lists six profiles | ✔️ pass | 6 rows |
| one curve is marked derived | ✔️ pass | 1 derived |
| the torque curve drew its handles | ✔️ pass | 8 handles |
| the curve editor drew the interpolated line | ✔️ pass | 2 paths |
| the thermal section shows its own coolant curve | ✔️ pass | 6 handles |
| the selected control point is named | ✔️ pass | Point 5 of 8 — drag it on the chart, or type here. |
| point Y pill reads the authored value | ✔️ pass | 640.0 |
| point X pill reads the authored value | ✔️ pass | 4500 |
| point X track reports its canonical value | ✔️ pass | 4500 |
| point pills use the curve axis as their extent | ✔️ pass | 2 fields |
| an arrow key nudges the selected point | ✔️ pass | point Y now 644.0 |
| the nudge moves the torque peak readout | ✔️ pass | 644Nm |
| Add point inserts into the selected curve | ✔️ pass | Point 8 of 9 — drag it on the chart, or type here. |
| the new point has a handle | ✔️ pass | 9 handles |
| Remove point deletes it again | ✔️ pass | Point 8 of 8 — drag it on the chart, or type here. |
| switching curve rebuilds the editor | ✔️ pass | 5 handles |
| the selected point follows the new curve | ✔️ pass | Point 2 of 5 — drag it on the chart, or type here. |
| one editor is drawn at a time | ✔️ pass | 1 panels |
| switching back restores the torque curve | ✔️ pass | 8 handles |
| the revision counter advanced | ✔️ pass | revision 4 |
| imperial units convert the mass pill | ✔️ pass | 2831 lb |
| the mass unit label follows | ✔️ pass | lb |
| the hero converts torque to lb·ft | ✔️ pass | 472lb·ft |
| the wheelbase card converts to inches | ✔️ pass | 107inWheelbase |
| the point pill converts its axis | ✔️ pass | 302.4 lb·ft |
| metric restores the mass pill | ✔️ pass | 1284 |
| detaching moves all four tyres to the pit lane | ✔️ pass | 4 in the pit lane |
| the vehicle record keeps its row | ✔️ pass | vehicle row present |
| the outliner status counts the detached tyres | ✔️ pass | Records10Enclosed0Detached4SceneDefault.levelspace |
| the tyre card reports the detachment | ✔️ pass | FL · Medium186kPaDetached · in the pit lane |
| the log records the pit stop | ✔️ pass | logged |
| re-attaching returns the tyres to the vehicle | ✔️ pass | pit lane empty |
| four tyres are enclosed again | ✔️ pass | 4 nested |
| fitting set B changes every compound | ✔️ pass | FL · Soft · FR · Soft · RL · Soft · RR · Soft |
| fitting a set resets the wear readouts | ✔️ pass | 0 % wear |
| the outliner metadata follows the compound | ✔️ pass | Soft · 186 kPa · 62 °C |
| the tyre record opens its own inspector | ✔️ pass | Tyre · FR |
| the tyre page carries an enclosure pill | ✔️ pass | ENCLOSED |
| the tyre page shows the compound window | ✔️ pass | 4 cells |
| the tyre page has editable fields | ✔️ pass | 18 rows |
| back returns to the vehicle record | ✔️ pass | Vehicle · GT-04 |
| rebuilding the page does not duplicate sections | ✔️ pass | 11 cards |
| rebuilding the page keeps one curve editor | ✔️ pass | 8 handles |
| the controls still answer after a rebuild | ✔️ pass | 126 rows |
| the record writes the tyre tables | ✔️ pass | tyre table present |
| the record writes the induction and supercharger table | ✔️ pass | induction table present |
| the record writes the soft-body FEM and slip table | ✔️ pass | soft_body table present |
| the record writes the suspension and Jolt substep tables | ✔️ pass | suspension & simulation tables present |
| the record writes the curves as point arrays | ✔️ pass | curve points present |
| the record writes the enclosure | ✔️ pass | enclosed = true |
| the record is marked proposed | ✔️ pass | proposed marker |
| the record has no NaN | ✔️ pass | clean |
| colour coding starts on the labels | ✔️ pass | colour-labels |
| the header carries a colour switch | ✔️ pass | 3 options |
| units mode is a body class | ✔️ pass | colour-units |
| plain mode is a body class | ✔️ pass | colour-none |
| labels mode comes back | ✔️ pass | colour-labels |
| a volume card carries its family | ✔️ pass | 3 volume elements |
| a temperature card carries its family | ✔️ pass | 13 temperature elements |
| a thermal card tones its own number | ✔️ pass | instrument-value tone-warm \| instrument-value tone-warm \| instrument-value tone-live \| instrument-value tone-cold |
| a fresh set reads cold against its window | ✔️ pass | instrument-value tone-cold \| instrument-value tone-cold \| instrument-value tone-cold \| instrument-value tone-cold |
| the c key steps to units | ✔️ pass | colour-units |
| the c key steps to plain | ✔️ pass | colour-none |
| the c key wraps to labels | ✔️ pass | colour-labels |

**🟢 all checks passed**
