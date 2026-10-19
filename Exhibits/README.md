# Exhibits

Retain validation code, actual captures, and reproducible results here.

```text
<ValidationName>/
├── README.md   # Claim, reproduction steps, environment, revision, limitations
├── Code/       # Code producing retained results
├── Images/     # Actual captures when applicable
└── Results/    # Tables, reports, and measurements
```

Use technical names conforming to the naming skill. Do not create empty evidence placeholders.
`EditorMirror/` is reserved for the C++ editor UI comparison mirror; ordinary prototypes are Experimental.
Reuse production engine code rather than copy it. Do not claim a mirror proves native rendering parity.
See `Documentation/Rules/Evidence.md` at repository root.

## Current validation

- `ControlCentre/`: typography/theme interactions, downloaded font inventory, Windows package inspection.
- `UIFoundation/`: historical first-batch foundation, lifecycle, glyph, and CPU raster evidence.

- `SpaceFormats/index.html`: single-page pinned-source audit, plan, original brief, and embedded native UI evidence.

- `ContentBrowser/`: read-only native browsing, shared expansion control, real-file tests and CPU captures.

- `Renderer/`: actual native Vulkan/Slang CAD readbacks, ABI/input/lifecycle checks and portability evidence.

- `TextureStudio/`: shared material-library IO and native ImGui interactions, CPU command captures and Windows object compilation.
