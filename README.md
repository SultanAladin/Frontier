# Frontier

General-purpose, cross-platform editor with reusable engine code.
EngineUpdate establishes documentation, audits inherited violations, and repairs code in reviewed batches.

## Start here

- Read [agent instructions](CLAUDE.md) and the four mandatory skills before contributing.
- Follow the [folder map](Documentation/FolderStructure.md) and [workflow](Documentation/Rules/AgentWorkflow.md).
- Track all progress and findings in the [single tracker](Documentation/Tracker.md).
- Follow the [active plan](Documentation/Plans/Ongoing/EngineUpdate.md).
- Retain reproducible evidence under [Exhibits](Exhibits/README.md).
- Build the [Control Centre, fonts, and themes](Exhibits/ControlCentre/README.md).

## Baseline status

Approved source baseline: `SultanAladin/Frontier-` at `338293863d7df4e585dec0c4acae45fe9c524ed5`.
This checkout is `SultanAladin/Frontier`, initially at `4a77bfd17f23536e32781718553ec6a171e39021`.
A selective UI foundation and Control Centre port is now present; the rest of the linked engine has not been
imported or merged.
See [port evidence and Windows build steps](Exhibits/UIFoundation/README.md).
See `git status` for the current working branch.

## Builds and binaries

The user runs Windows; Windows binaries are the default requested binary deliverable.
Do not build or package Linux binaries unless asked. Preserve cross-platform source portability.
MSVC is the current user toolchain; supported GCC, MinGW-w64, and Clang configurations are also permitted.
No macOS setup or binary downloads are currently in scope. No Android application exists in this scope.

The new root CMake target builds only the selected UI foundations and Windows UI exhibit, not the old engine.
Six local checks passed in Debug and with sanitizers. Windows x64 cross-compilation succeeded.
Windows runtime/GPU execution and native MSVC verification remain outstanding.
Do not mix inherited build instructions with this selective target.
Local verification is automatic; GitHub Actions require explicit permission.

## Fonts

Run `python -m pip install -r Automation/FontRequirements.txt`, then `python Automation/PrepareFonts.py`.
This downloads four OFL-licensed families and generates native TOML and tooling JSON font catalogues.
Downloaded fonts remain ignored; the Windows package includes them and their licences.
The Control Centre exposes only interface appearance settings, not GI or renderer controls.

Primary rendering: Vulkan with Slang shaders. GLFW owns windows/input; the isolated ImGui exhibit may use DX11.
The Control Centre uses the reference trapezoidal notch and upward swipe-to-close drawer.

## Landscape authoring prototype

[Launch and workflow](Experimental/LandscapeWorkspace/README.md) ·
[actual captures and reproducible validation](Exhibits/LandscapeWorkspace/README.md).

The isolated browser editor uses the pinned ProjectZero presentation, vector-mask authoring, independently scoped
3D landform stacks, a sea-level foundation, procedural ocean waves and sparse metric-distance export.
Landscape extracts bounded XYZ surfaces off-thread and rasterizes them on the GPU: **not a heightmap, no software
fallback**. Each allocated region has independent XYZ sampling counts, including 96 vertical cells.
**Clay/Carve sculpting** stores region-owned XYZ strokes with atomic undo, save/import and volume export.
Field-resampled adaptive **High detail** is now the default; sculpting and close zoom request native-pitch working
areas automatically. Actual completed geometry stays distinct from targets and image pixels. The 120k-triangle
budget does not make a whole 2048³ lattice resident; complete volume paging remains open.
World-space waves no longer retune with camera distance. Graphics failure stops the preview while preserving Mask and
save; restart is explicit.
The initial Mask workspace creates no hidden 3D workload. Focused sculpt checks pass, but the broad editor regression
still hit the four-second GPU fence stop; overall GPU stability is not established. Smooth/Flatten and simulations
are not added. This is a browser prototype, not a native-editor replacement or physical Windows verification.

```bash
cd Experimental/LandscapeWorkspace
npm ci
npm run dev -- --port 5173
```
