# SceneObject Validation Exhibit

🧩 Validation of the SceneObject model (`Engine/SceneObject/`) and a compile check of its ControlKit inspector
(`UI/Editor/SceneObjectInspector.cpp`).

## Reproduce

```bash
bash Exhibits/SceneObjectValidation/Code/build.sh
```

Prerequisites: Linux with `g++` and C++17. The model needs no ImGui and no toml++. The inspector check needs only the
ControlKit headers in `Engine/Interface/`.

The script compiles the model with `-Wall -Wextra -Werror`, runs the validation, and writes the output to
`Results/SceneObjectValidation-results.txt`.

## What is tested

| Area | Checks |
|---|---|
| Template | six parts in build order; emitter links host 3 and data 4; template validates |
| Derived value | pole length = Lamp Height − Base Height (expected 7.5 m; 11.5 m after a 12 m edit) |
| Clamped edits | lamp height clamps to 3 m and 20 m; colour clamps to 0..1; IES choice clamps to the last option |
| Refused edits | misspelt name → `UnknownProperty`; colour as number → `WrongPropertyKind`; bad index → `IndexOutOfRange` |
| Links | arm on plinth, shaft without host, light on plinth, light without profile, plinth with a host are refused |
| Removal | a part in use is `InUse`; an unused part is removed and later links shift down by one |
| Checked loading | a list with a forward host, an out-of-range value, or a missing value is refused and leaves the store unchanged |

Mutation check (run once, not retained): with the clamp in `SetNumber` removed, the lamp height checks fail (2 failed,
34 passed). The checks detect that defect.

## Results

See `Results/SceneObjectValidation-results.txt`. 36 passed, 0 failed, on the revision below.

## Revision and changes

- Base revision: `e016a0bf43e7229254e7e88847cd31980ea17a24` on branch `arena/67f1c409-frontier`.
- Uncommitted: `Engine/SceneObject/*`, `UI/Editor/SceneObjectInspector.*`, `Engine/Interface/ControlKit.cpp`
  (dropdown colours), this exhibit.

## Limitations

- Linux `g++` only. No Windows binary was built or run. Windows delivery is not verified by this exhibit.
- The inspector is compile-checked only. It is not rendered: `PixelSpace` records into ImGui draw lists, and ImGui is not
  in this checkout (`ExternalPackages/` is absent). No native screenshot exists for the inspector.
- Colour is edited through five presets, not a free colour picker.
- Saved-file read and write are not implemented. `TryAdoptParts` checks a list that a loader supplies.
- The ControlKit dropdown colour change is visible in `AppearanceInspector` too. It has not been checked in a native render.
