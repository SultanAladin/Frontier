# Composed SceneObjects — flat parts, one inspector, Streetlight first

**Status:** 🚧 Implemented in C++ (model and inspector); web prototype kept for comparison
**Scope:** Production editor (`Engine/SceneObject/`, `UI/Editor/`) and the Construct menu. First template: Streetlight.
**Evidence:** `Exhibits/SceneObjectValidation/` (36 checks, Linux `g++`). Web checks in §9.

---

## 1. Summary

| Question | Answer |
|---|---|
| What is built? | A SceneObject: one ordered list of six named parts, with every link an index into that list. |
| Nested? | No. A part never owns a part. Sockets and data links are indices, checked on add and on load. |
| Editable? | Every stored value of every part has a descriptor and a ControlKit widget. Nothing is hidden. |
| Dropdowns | 🔴 One style for every choice control: charcoal value segment, darker chevron segment, white chevron. |
| Where does it appear? | Construct menu, category **Installations**, entry **Streetlight**. |
| Actor? | Not used. |

---

## 2. Naming

**System name: `SceneObject`.** You named it so. `Object` is on the banned generic-filler list in SKILL-Naming §1,
so the exception is recorded in `SYSTEM_NOMENCLATURE.md` §1 as **E2**, scoped to this system only, and tracked as
tracker item 018 for review.

| Candidate | Outcome |
|---|---|
| Assembly | 🔴 rejected by you ("terrible names") |
| Composition | 🔴 rejected by you |
| Fixture | 🔴 rejected by you |
| Kit | 🔴 rejected by you |
| Installation | 🔴 superseded by SceneObject |
| **SceneObject** | ✔️ chosen; exception E2 |

**Construct category:** `Installations`, its own section.

### 2.1 Identifiers as implemented

| Role | Identifier | File |
|---|---|---|
| vocabulary | `SceneObjectPartClass`, `SceneObjectSocket`, `SceneObjectPropertyKind`, `SceneObjectResult` | `Types.h` |
| property metadata | `SceneObjectPropertyDescriptor`, `SceneObjectPropertySpan` | same |
| one flat row | `SceneObjectPartEntry` | same |
| the flat list | `SceneObjectPartStore` | `Engine/SceneObject/SceneObjectPartStore.h` |
| Streetlight template | `MakeStreetlightSceneObject` | `Engine/SceneObject/StreetlightSceneObject.h` |
| inspector | `SceneObjectInspector`, `SceneObjectInspectorState` | `UI/Editor/SceneObjectInspector.h` |

Part classes keep their names as identifiers, because `Base` is banned. The user-facing label is in brackets:

| Part class | Identifier (saved name) | Label shown |
|---|---|---|
| `Plinth` | `PlinthPart` | Base |
| `Shaft` | `ShaftPart` | Pole |
| `Arm` | `ArmPart` | Attachments |
| `Housing` | `HousingPart` | Headlamp |
| `Emitter` | `EmitterPart` | Light |
| `PhotometricProfile` | `PhotometricProfile` | IES Profile |

---

## 3. Flat structure

The store is one array in build order. No entry lists its children.

```text
SceneObjectPartStore  (one array; index order is build order)
  idx  class                  host idx  host socket  data idx  notes
  ---  ---------------------  --------  -----------  --------  -----------------------------
   0   Plinth (PlinthPart)      -1       none          -1      root; sits on the ground
   1   Shaft (ShaftPart)         0       Top           -1      Lamp Height lives here
   2   Arm (ArmPart)             1       Head          -1      carries the headlamp
   3   Housing (HousingPart)     2       Tip           -1      case dimensions
   4   PhotometricProfile       -1       none          -1      distribution data
   5   Emitter (EmitterPart)     3       Aperture       4      intensity, colour, radius
```

Rules, all enforced by `SceneObjectPartStore`:

- 🔴 A host or data index must name an **earlier** entry of the required class. The list is always a valid build order.
- 🔴 Links are indices. Removing an entry that another entry names is refused (`InUse`). Removing an earlier
  entry shifts later links down by one.
- ⚠️ A corrupt list is refused by `TryAdoptParts` and the store is left unchanged.

---

## 4. Properties

Every stored value has a descriptor in `SceneObjectTypes.cpp`. The inspector, the validator and the templates all read
the same table.

| Part | Property | Unit | Range | Default |
|---|---|---|---|---|
| Base | Base Width | m | 0.3–1.2 | 0.45 |
| Base | Base Height | m | 0.1–1.0 | 0.5 |
| Pole | Lamp Height | m | 3–20 | 8 |
| Pole | Pole Diameter | m | 0.05–0.4 | 0.14 |
| Attachments | Arm Length | m | 0–3 | 1.2 |
| Attachments | Arm Tilt | ° | 0–30 | 8 |
| Headlamp | Case Length | m | 0.2–1.2 | 0.6 |
| Headlamp | Case Width | m | 0.1–0.6 | 0.3 |
| Headlamp | Case Height | m | 0.05–0.4 | 0.15 |
| Light | Intensity | cd | 0–50000 | 12000 |
| Light | Colour | sRGB | 0–1 per channel | #ffd9a0 |
| Light | Light Radius | m | 0.01–0.5 | 0.05 |
| IES Profile | IES Profile | choice | 4 options | Type II · roadway |

💡 Lamp Height is measured from the ground to the lamp. Pole length is derived as `Lamp Height − Base Height`
(`PoleLength`), so it is never a second input.

---

## 5. Inspector (ControlKit)

The inspector uses ControlKit, not ImGui. ControlKit is the production widget kit in `Engine/Interface/`, and it
has the dropdown you specified.

| Descriptor | ControlKit widget | Notes |
|---|---|---|
| Length, Radius | value pill + `Slider` | pill shows two decimals and the unit |
| Angle, Intensity | value pill + `Slider` | whole numbers |
| Colour | five `Swatch` presets | 🔴 presets only; a free colour picker is not in ControlKit yet |
| Choice | `Dropdown` + `DropdownMenu` | menu drawn in a second pass, so it overlays the cards |

🔴 **Dropdown style.** `ControlKit::Dropdown` now draws the value segment in `Palette().Selected` (charcoal), the
caret segment in `Palette().Inset` (darker; `Raised` on hover), and the chevron in `Palette().Primary` (white). Before
this change the value segment was black and the chevron grey, which did not match the spec. `AppearanceInspector` uses
the same widget, so its dropdowns change too. The web prototype has the same style (see §9).

Every part is one card. There is no tree; the host is shown as text on the card, not as indentation.

---

## 6. Construct integration

| Item | Decision |
|---|---|
| Category | **Installations**, own section |
| Entry | **Streetlight**, template `MakeStreetlightSceneObject()` |
| Gate | ungated, like band 10 in `PLAN-ConstructionCatalogueMenu.md` §4 |
| Band | 12th row added to the plan's §4 table, marked outside the 128 CAD-operation total |

The C++ Construct menu is not yet implemented, so the entry is reachable only through the web prototype (§9).

---

## 7. Implementation status

| Piece | Path | Status |
|---|---|---|
| Vocabulary, descriptor tables, host rules | `Engine/SceneObject/SceneObjectTypes.{h,cpp}` | ✔️ built, tested |
| Flat store: add, remove, edit, validate, load | `Engine/SceneObject/SceneObjectPartStore.*` | ✔️ built, tested |
| Streetlight template | `Engine/SceneObject/StreetlightSceneObject.{h,cpp}` | ✔️ built, tested |
| ControlKit inspector | `UI/Editor/SceneObjectInspector.{h,cpp}` | ✔️ compiles; 🔴 not rendered |
| ControlKit dropdown style | `Engine/Interface/ControlKit.cpp` | ✔️ changed; not rendered natively |
| Saved-file read and write | — | ⚠️ not started |
| C++ Construct menu entry | — | ⚠️ not started |

---

## 8. Undo and persistence (planned)

- Each inspector edit should be one undo step, keyed by part index and descriptor name.
- The saved form is the flat list of §3, in build order. No nested objects.
- A file with an unknown class or a broken link is refused as a whole, through `TryAdoptParts`.

---

## 9. Web prototype

Kept in `Experimental/ProjectZeroEditor/` as a comparison and for the Construct menu.

| File | Change |
|---|---|
| `StreetlightSpecification.js` | the sheet, the Construct entry, the outliner summary |
| `Dropdown.jsx` / `Dropdown.css` (new) | one dropdown: charcoal value, darker chevron segment, white chevron |
| 26 `<select>` sites across 11 files | now `<Dropdown>`; native select inside, so handlers are unchanged |
| `GeometryFields.jsx` | `SplitSelect` is a thin wrapper over `Dropdown` |
| `CheckStreetlight.mjs` (new) | category, entry, flat groups, edit reaches outliner, no page errors |

The web checks run on the rebuilt bundle. `CheckStreetlight` and `CheckGeometry` pass. The full suite is compared
against the earlier baseline in `_ClaudeScratch/modified3.txt`.

---

## 10. Rule compliance

| Rule | Status |
|---|---|
| Plans only in `Plans/Ongoing/` when approved | ✔️ this file, approved by you |
| Shared code in `Engine/`, UI in `UI/Editor/`, validation in `Exhibits/` | ✔️ |
| Exhibits retain code, results and instructions | ✔️ `Exhibits/SceneObjectValidation/` |
| Banned words (identifiers) | ✔️ except `SceneObject`, which is exception E2 |
| Actor | ✔️ not used |
| Commits and pushes | ✔️ none; waiting for your approval |
| Windows binary | ⚠️ not built; Linux `g++` only |
