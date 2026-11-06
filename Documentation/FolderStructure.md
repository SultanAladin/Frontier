# Folder Structure

## Target ownership

```text
Frontier/
├── CMakeLists.txt             # UI-only build and local verification
├── Automation/                # Existing tooling plus pinned UI setup and patches
├── ExternalPackages/          # Ignored downloaded dependency trees
├── README.md
├── AGENTS.md
├── CLAUDE.md
├── Documentation/
│   ├── Tracker.md
│   ├── FolderStructure.md
│   ├── Rules/
│   └── Plans/
│       ├── Ongoing/
│       ├── Completed/
│       └── References/        # Retained reference documents (InheritedEngine/)
├── Engine/                    # Shared production code
│   ├── Interface/             # Selectively ported UI foundations
│   ├── Lifecycle/             # Shared activation sequence
│   └── Render/                # Blank; Git retention marker only
├── UI/
│   └── Editor/                # Lifecycle-owned production editor UI
├── EngineContent/             # Shared content archives (icons, fonts, materials, environments)
├── Exhibits/
│   ├── <ValidationName>/
│   │   ├── README.md
│   │   ├── Code/
│   │   ├── Images/
│   │   └── Results/
│   └── EditorMirror/           # Comparison mirror of the C++ editor
├── Experimental/              # Prototypes and experimental HTML
├── EngineDocs/AgenticInstuctions/  # Four retained authoritative skills
└── _ClaudeScratch/            # Ignored logs/, build/, tmp/
```

This is a target map, not a claim that source migration is complete.
Create meaningful folders as their contents are needed; do not add empty placeholder trees.
Use technical validation names under Exhibits, not the banned naming placeholder from earlier discussion.
No new top-level ownership categories without approval. Update this map after approved structural changes.

## Inherited layout — migration review outcome (2026-10-22)

`Internal/`, `Executables/`, `Applications/`, and `memory/` were reclaimed from this branch; the
complete trees remain on `master` and in this branch's history. Their documents, together with
`Documentation/Prototypes/`, `Documentation/Research/`, and the inherited root-level plan and
reference notes, are retained under `Documentation/Plans/References/InheritedEngine/`.
`Automation/` is current tooling (see the target map). Existing diagnostics remain in
`Exhibits/Diagnostics/`. `EngineDocs/` retains existing governance references until separately
reconciled.
The linked source also has `Docs/`, `Editor/`, `Projects/`, `Tools/`, and dependency/content folders;
map their ownership and build dependencies before any import or relocation.

## Selective port boundary

`Exhibits/UIFoundation/` retains verification code, results, and actual SVG raster evidence.
`Automation/Patches/` owns the six inherited ImGui patches. Dependency pins live in
`Automation/UIDependencies.json`. No scene renderer is ported or built by the new CMake target.
No inherited `Internal/` code is linked into this target.

`Exhibits/ControlCentre/` owns current appearance interaction tests, native CPU images, and build evidence.
`Automation/FontSources.json`, `PrepareFonts.py`, and `FontRequirements.txt` define open-font recovery.
`Automation/WindowsToolchain.cmake` supports Windows cross-compilation on the editing host.
`Automation/PackageWindows.py` packages only Windows binaries and their runtime assets under ignored `Binaries/`.

## Shared content and dependencies

```text
EngineContent/
├── Icons/                     # Tracked original/compatibility SVGs
├── MaterialArchives/          # Tracked Texture Studio material library (.material + baked maps)
├── PatternArchives/           # Tracked Texture Studio pattern presets
├── EnvironmentArchives/       # Tracked baked equirect studio environments (+ user imports)
├── Defaults/                  # Tracked blank project space
└── FontArchives/<font name>/  # Ignored font downloads, OFL notices, TOML descriptors
ExternalPackages/
├── glfw/                      # GLFW 3.4 platform windows/input
├── tomlpp/                    # Native TOML formats
├── imgui/                     # Pinned UI + approved patches
└── thorvg/                    # SVG rasterization
```

ContentDirectory resolves bundled content from the executable independently of the launch working directory.
The font catalogue has a native TOML form; JSON remains only an automation/report interchange.

## Landscape browser prototype

`Experimental/LandscapeWorkspace/` owns the requested vector-mask canvas, bounded volumetric SDF preview,
procedural WebGL2 ocean, tests, and launcher configuration. `Exhibits/LandscapeWorkspace/` retains its browser
interaction script, results, and actual application captures. This prototype does not replace native editor code.


`Experimental/LandscapeWorkspace/ReferenceSnapshot/` retains the pinned, read-only landform reference files and source
fingerprints. Mask-owned geometry lives in `LandformStack.js`, its GLSL counterpart in `LandformShader.js`, and
stack/edge
editing in `LayerStackPanel.js`. `LandformPresets.js` owns recipe data; `PresetPreviews/` holds actual SDF captures
reproduced through `PresetExamples.html`. `Exhibits/LandscapeWorkspace/Landforms.md` owns the focused validation report.

`Experimental/LandscapeWorkspace/TerrainNoise.js` contains matching CPU/GLSL volumetric lattice/octave fields;
`TerrainExamples.js` creates eight already-applied terrain masks. `CameraNavigation.js` owns optional keyboard movement
and camera-tool selection. Current source-aligned UI/navigation evidence is in
`Exhibits/LandscapeWorkspace/TerrainGenerators.md` and `TerrainBrowserValidation.mjs`.

`Experimental/LandscapeWorkspace/ReferenceStack.css` mechanically preserves the upstream stack/inspector CSS cascade;
`StackDock.css` integrates those components with mask-owned stacks; `StackGlyphs.js` supplies the same Lucide glyphs.
`Exhibits/LandscapeWorkspace/ExtractReferenceStackStyle.mjs` reproduces extraction.
`ReferenceStackBrowserValidation.mjs` compares the original row template/full stylesheet with the working component
and exercises source-style interactions. Current captures/report: `ReferenceLayerStack.md` in the same exhibit.

`Experimental/LandscapeWorkspace/TerrainInfluence.js` owns compact radial terrain profiles and matching GLSL;
`TerrainInfluence.test.js` verifies flat support, non-folding terrain, gradient bounds and serialization.
`Workspace.js` switches distinct mask-document and landform-operation panes. Current evidence:
`Exhibits/LandscapeWorkspace/RadialTerrain.md`, `RadialStackBrowserValidation.mjs`, `RadialDuneInfluence.png` and
`SeparateMaskStack.png`. Legacy structures remain in the signed-volume primitive catalogue.
