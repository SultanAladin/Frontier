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
│       └── Completed/
├── Engine/                    # Shared production code
│   ├── ContentInterchange/    # Shared authoring contracts, editable codecs and artwork operations
│   ├── Interface/             # Selectively ported UI foundations
│   ├── Lifecycle/             # Shared activation sequence
│   └── Render/                # Shared Vulkan/Slang coverage and material rasterization
├── UI/
│   ├── Editor/                # Lifecycle-owned production editor UI
│   └── TextureStudio/         # Workspace panels using shared Engine mechanisms
├── EngineContent/             # Shared Icons/ and downloaded FontArchives/
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

## Existing layout awaiting migration review

`Internal/`, `Executables/`, `Applications/`, `Automation/`, `memory/`,
`Documentation/Prototypes/`, and `Documentation/Research/` remain inherited locations.
Do not delete or move them blindly. Existing diagnostics remain in `Exhibits/Diagnostics/`.
`EngineDocs/` retains existing governance references until separately reconciled.
The linked source also has `Docs/`, `Editor/`, `Projects/`, `Tools/`, and dependency/content folders;
map their ownership and build dependencies before any import or relocation.

## Selective port boundary

`Exhibits/UIFoundation/` retains verification code, results, and actual SVG raster evidence.
`Automation/Patches/` owns the six inherited ImGui patches. Dependency pins live in
`Automation/UIDependencies.json`. The initial UI-only port did not import the inherited scene renderer.
Separately authorised shared `Engine/Render/` units now build alongside the UI.
Inherited `Internal/Graphics/` remains untouched and is not linked into this target.

`Exhibits/ControlCentre/` owns current appearance interaction tests, native CPU images, and build evidence.
`Automation/FontSources.json`, `PrepareFonts.py`, and `FontRequirements.txt` define open-font recovery.
`Automation/WindowsToolchain.cmake` supports Windows cross-compilation on the editing host.
`Automation/PackageWindows.py` packages only Windows binaries and their runtime assets under ignored `Binaries/`.

## Shared content and dependencies

```text
EngineContent/
├── Icons/                     # Tracked original/compatibility SVGs
└── FontArchives/<font name>/  # Ignored font downloads, OFL notices, TOML descriptors
ExternalPackages/
├── glfw/                      # GLFW 3.4 platform windows/input
├── tomlpp/                    # Native TOML formats
├── imgui/                     # Pinned UI + approved patches
└── thorvg/                    # SVG rasterization
```

ContentDirectory resolves bundled content from the executable independently of the launch working directory.
The font catalogue has a native TOML form; JSON remains only an automation/report interchange.

## Inherited launchers and editor ownership

| Existing path                                  | Ownership and dependency trace                          |
|------------------------------------------------|---------------------------------------------------------|
| Applications/InterfaceValidation/...            | Older standalone Win32/DX11 validation; Internal UI.     |
| Executables/                                   | Older launchers/validations; bespoke build scripts.      |
| Internal/                                      | Older authoring, interface, graphics and platform code.  |
| Engine/Interface/                              | Current reusable controls, catalogue and motion.        |
| UI/Editor/                                     | Current lifecycle-owned, editor-specific panels.         |

The current CMake targets do not link Applications, Executables or Internal. Older build scripts still reference
Internal libraries, and similarly named LayerStackValidation hosts are not byte-identical. This is not evidence
that all inherited code is dead or safe to delete. A separate migration/dependency review precedes any removal.
Keep UI/Editor separate: editor panels depend on shared Engine/Interface mechanisms, not the reverse.
`Exhibits/ContentBrowser/` owns browser verification code, actual native captures and reference adaptation notes.

## Project/default-content ownership

- `Engine/ContentInterchange/ProjectSpaceCodec.{h,cpp}` owns the scoped editable project container codec.
- `EngineContent/Defaults/BlankProject/Blank.projectspace` is the installed template and shared engine defaults.
- The project container embeds appearance, input, notification and engine configuration; no project TOML sidecars.
- Font/icon payloads remain in their existing shared folders. Defaults contain references, not duplicate fonts.
- Appearance/Input codecs expose shared table operations; legacy standalone APIs remain compatibility paths.
- Each project owns its editable `<Name>.projectspace`. Personal overrides use a separate `Personal.projectspace`.
- `Exhibits/ProjectSpace/` owns actual native captures and production-code persistence validation.
- Binary runtime packages and other Space formats are separate work, not implied by the editor container extension.

## Notification cards and session history

- `Engine/Interface/NotificationSequence.{h,cpp}`: reusable bounded alert storage and shared semantic card geometry.
- `UI/Editor/ControlCentrePanel.{h,cpp}`: actual preference events, top-right placement/input ownership, history UI.
- `Exhibits/Notifications/`: native validation source, nine actual CPU-rendered images, contract and limitations.

- `Engine/Interface/ConfirmationDialogue.{h,cpp}`: reusable semantic modal presentation and response, shared controls.
- Quick Controls owns the responsive Notification Stream; notification settings contain preferences only.
